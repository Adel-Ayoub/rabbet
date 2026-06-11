#include "editor/Editor.h"

#include "editor/ComponentDrawers.h"
#include "editor/Palette.h"
#include "editor/panels/AssetsPanel.h"
#include "editor/panels/ConsolePanel.h"
#include "editor/panels/HierarchyPanel.h"
#include "editor/panels/InspectorPanel.h"
#include "editor/panels/ViewportPanel.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetHandle.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Clock.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/PhysicsSystem.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/render/AssetResolveSystem.h"
#include "rabbet/render/DebugDraw.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/Image.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelImport.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/render/RenderSystem.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/Skybox.h"
#include "rabbet/render/SkyboxSystem.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/render/gl/Cubemap.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/LightSystem.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/scripting/ScriptAsset.h"
#include "rabbet/scripting/ScriptAssetResolveSystem.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptImport.h"
#include "rabbet/scripting/ScriptSystem.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>

#include <ImGuizmo.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <imgui_internal.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#ifndef RB_EDITOR_ASSETS
#define RB_EDITOR_ASSETS "assets"
#endif

namespace rb::editor {
namespace {

// Loads a cubemap from six face images named right/left/top/bottom/front/back
// (+X,-X,+Y,-Y,+Z,-Z). Missing faces fall back to a flat sky-blue so the editor
// still opens if an image is absent.
rb::gl::Cubemap loadCubemap(const std::filesystem::path& dir) {
    const std::array<std::string, 6> faces = {"right.jpg", "left.jpg",   "top.jpg",
                                              "bottom.jpg", "front.jpg", "back.jpg"};
    std::array<rb::Image, 6> images;
    for (std::size_t i = 0; i < 6; ++i) {
        if (std::optional<rb::Image> image = rb::loadImage(dir / faces[i], false)) {
            images[i] = std::move(*image);
        } else {
            rb::Image fallback;
            fallback.width = 1;
            fallback.height = 1;
            fallback.channels = 3;
            fallback.pixels = {std::byte{90}, std::byte{120}, std::byte{170}};
            images[i] = std::move(fallback);
        }
    }
    return rb::gl::Cubemap::fromFaces(images);
}

} // namespace

Editor::Editor(rb::Window& window, rb::RenderDevice& device)
    : m_window(window), m_device(device), m_context{m_runtime, m_registry} {
    registerBuiltinComponents(m_registry);
    registerComponentDrawers(m_registry); // editor-side ImGui hooks for each builtin
    m_log.install(); // capture engine logs into the console (and stderr)

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = "rabbet_forge.ini"; // own layout file; don't inherit a stale imgui.ini
    applyStyle();
    loadFonts();
    ImGui_ImplGlfw_InitForOpenGL(m_window.handle(), true);
    ImGui_ImplOpenGL3_Init("#version 410");

    m_panels.add<HierarchyPanel>(m_context);
    m_panels.add<ViewportPanel>(m_context);
    m_panels.add<InspectorPanel>(m_context);
    m_panels.add<ConsolePanel>(m_context, m_log);
    m_panels.add<AssetsPanel>(m_context);
}

Editor::~Editor() {
    rb::log::resetSink(); // detach the console sink before m_log is destroyed
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::buildDefaultScene() {
    m_runtime.addResource<rb::Viewport>();
    m_runtime.addResource<rb::RenderView>();
    rb::AssetManager& assets = m_runtime.addResource<rb::AssetManager>();
    rb::AssetDatabase& database = m_runtime.addResource<rb::AssetDatabase>();
    rb::Lighting& lighting = m_runtime.addResource<rb::Lighting>();
    lighting.ambient = glm::vec3(0.32f, 0.34f, 0.40f);
    m_runtime.addResource<rb::DebugDraw>(); // collider gizmo toggle (on by default)

    // ScriptSystem (Play phase) runs before TransformSystem so a script's transform edits
    // are baked into world matrices the same frame; its resolve system (Always) keeps the
    // .lua handle current and polls for hot reloads.
    m_runtime.addSystem<rb::ScriptAssetResolveSystem>();
    m_runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    m_runtime.addSystem<rb::PhysicsSystem, rb::SystemPhase::Play>();
    m_runtime.addSystem<rb::TransformSystem>();
    m_runtime.addSystem<rb::LightSystem>();
    m_runtime.addSystem<rb::AssetResolveSystem>();
    m_runtime.addSystem<rb::SkyboxSystem>();
    m_renderSystem = &m_runtime.addSystem<rb::RenderSystem>();

    const std::filesystem::path assetsRoot{RB_EDITOR_ASSETS};

    // Real sky: a cubemap skybox.
    m_runtime.addResource<rb::Skybox>(rb::Skybox{loadCubemap(assetsRoot / "skybox/sky"),
                                                 rb::gl::Mesh::create(rb::geometry::cube())});

    rb::Scene& scene = m_runtime.scene();

    // Ground plane (a flattened primitive).
    const rb::Entity floor = scene.create();
    scene.add<rb::Name>(floor, rb::Name{"Floor"});
    rb::Transform floorTransform;
    floorTransform.position = glm::vec3(0.0f, -0.9f, 0.0f);
    floorTransform.scale = glm::vec3(18.0f, 0.3f, 18.0f);
    scene.add<rb::Transform>(floor, floorTransform);
    scene.add<rb::Primitive>(
        floor, rb::Primitive{rb::PrimitiveShape::Cube, glm::vec3(0.58f, 0.60f, 0.64f), 0.0f, 0.95f});
    // Static physics floor matching the visual box, so dropped bodies land on it.
    scene.add<rb::RigidBody>(floor, rb::RigidBody{rb::BodyType::Static, 0.0f, 0.4f, 0.0f, true});
    scene.add<rb::BoxCollider>(floor, rb::BoxCollider{glm::vec3(9.0f, 0.15f, 9.0f), glm::vec3(0.0f)});

    // Real crate model loaded through the asset pipeline (assimp -> AssetManager),
    // referenced by uuid via ModelRenderer and resolved by AssetResolveSystem.
    const rb::AssetHandle<rb::ModelAsset> crate = rb::loadModelAsset(
        assets, assetsRoot / "models/wooden_crate_02/wooden_crate_02_1k.gltf", {0.0f, 0.7f});
    const rb::Entity crateEntity = scene.create();
    scene.add<rb::Name>(crateEntity, rb::Name{"Crate"});
    rb::Transform crateTransform;
    crateTransform.position = glm::vec3(0.0f, -0.75f, 0.0f);
    crateTransform.rotation = glm::angleAxis(glm::radians(28.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    crateTransform.scale = glm::vec3(3.5f);
    scene.add<rb::Transform>(crateEntity, crateTransform);
    rb::ModelRenderer renderer;
    renderer.model = assets.uuidOf(crate);
    renderer.handle = crate;
    scene.add<rb::ModelRenderer>(crateEntity, renderer);
    m_context.selected = crateEntity;

    // Attach a sample Lua behavior so pressing Play spins the crate (and hot-reloads when
    // the .lua is edited). The script's exposed fields are introspected for the inspector.
    const rb::AssetHandle<rb::ScriptAsset> spin =
        rb::loadScriptAsset(assets, assetsRoot / "scripts/spin.lua");
    if (spin.valid()) {
        rb::ScriptComponent script;
        script.script = assets.uuidOf(spin);
        script.handle = spin;
        if (const rb::ScriptAsset* asset = assets.get<rb::ScriptAsset>(spin)) {
            rb::introspectScriptFields(asset->source, script.fields);
        }
        scene.add<rb::ScriptComponent>(crateEntity, script);
    }

    const rb::Entity sun = scene.create();
    scene.add<rb::Name>(sun, rb::Name{"Sun"});
    rb::DirectionalLight sunLight;
    sunLight.direction = glm::normalize(glm::vec3(-0.55f, -0.85f, -0.5f));
    sunLight.color = glm::vec3(1.0f, 0.96f, 0.88f);
    sunLight.intensity = 2.6f;
    scene.add<rb::DirectionalLight>(sun, sunLight);

    const rb::Entity lamp = scene.create();
    scene.add<rb::Name>(lamp, rb::Name{"Lamp"});
    rb::Transform lampTransform;
    lampTransform.position = glm::vec3(2.4f, 2.0f, 2.6f);
    scene.add<rb::Transform>(lamp, lampTransform);
    rb::PointLight lampLight;
    lampLight.color = glm::vec3(0.6f, 0.75f, 1.0f);
    lampLight.intensity = 5.0f;
    scene.add<rb::PointLight>(lamp, lampLight);

    // Dynamic boxes stacked above the floor: pressing Play drops them onto it, and their
    // colliders draw as wireframe gizmos. Slight offsets and tilts so they tumble.
    const std::array<glm::vec3, 3> boxColors = {
        glm::vec3(0.85f, 0.35f, 0.30f), glm::vec3(0.35f, 0.65f, 0.85f),
        glm::vec3(0.85f, 0.75f, 0.35f)};
    for (int i = 0; i < 3; ++i) {
        const float fi = static_cast<float>(i);
        const rb::Entity box = scene.create();
        scene.add<rb::Name>(box, rb::Name{"Box " + std::to_string(i + 1)});
        rb::Transform boxTransform;
        boxTransform.position = glm::vec3(0.3f * fi - 0.3f, 2.0f + 1.4f * fi, 0.25f * fi);
        boxTransform.rotation =
            glm::angleAxis(glm::radians(14.0f * fi), glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
        scene.add<rb::Transform>(box, boxTransform);
        scene.add<rb::Primitive>(
            box, rb::Primitive{rb::PrimitiveShape::Cube, boxColors[static_cast<std::size_t>(i)],
                               0.0f, 0.6f});
        scene.add<rb::RigidBody>(box, rb::RigidBody{rb::BodyType::Dynamic, 1.0f, 0.4f, 0.15f, true});
        scene.add<rb::BoxCollider>(box, rb::BoxCollider{glm::vec3(0.5f), glm::vec3(0.0f)});
    }

    const std::size_t count = database.scan(assetsRoot, &assets);
    rb::log::info("editor: project assets catalogued: {}", count);
}

void Editor::buildDefaultLayout(unsigned int dockId) {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);

    ImGuiID center = dockId;
    const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
    const ImGuiID right =
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.24f, nullptr, &center);
    const ImGuiID bottom =
        ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.26f, nullptr, &center);

    ImGui::DockBuilderDockWindow("Hierarchy", left);
    ImGui::DockBuilderDockWindow("Inspector", right);
    ImGui::DockBuilderDockWindow("Console", bottom);
    ImGui::DockBuilderDockWindow("Assets", bottom); // tabbed with Console
    ImGui::DockBuilderDockWindow("Viewport", center);
    ImGui::DockBuilderFinish(dockId);
}

void Editor::drawDockspaceAndMenu() {
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
    ImGui::Begin("##RabbetDockHost", nullptr, flags);
    ImGui::PopStyleVar(3);

    const ImGuiID dockId = ImGui::GetID("RabbetDockSpace");
    if (ImGui::DockBuilderGetNode(dockId) == nullptr) {
        buildDefaultLayout(dockId);
    }
    ImGui::DockSpace(dockId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene")) {
                newScene();
            }
            if (ImGui::MenuItem("Open Scene")) {
                openScene();
            }
            if (ImGui::MenuItem("Save Scene")) {
                saveScene();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) {
                m_window.requestClose();
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View")) {
            m_panels.renderMenuItems();
            ImGui::Separator();
            if (ImGui::MenuItem("Reset Camera")) {
                m_camera.reset();
            }
            ImGui::EndMenu();
        }

        // Play controls.
        ImGui::TextUnformatted("   ");
        if (!m_playSession) {
            if (ImGui::Button("Play")) {
                startPlay();
            }
        } else {
            if (ImGui::Button(m_paused ? "Resume" : "Pause")) {
                m_paused = !m_paused;
            }
            ImGui::BeginDisabled(!m_paused);
            if (ImGui::Button("Step")) {
                m_step = true;
            }
            ImGui::EndDisabled();
            if (ImGui::Button("Stop")) {
                stopPlay();
            }
            ImGui::TextDisabled(m_paused ? "[paused]" : "[playing]");
        }
        ImGui::EndMenuBar();
    }
    ImGui::End();
}

void Editor::renderScene(int width, int height, float dt) {
    if (!m_framebuffer.has_value()) {
        m_framebuffer = rb::gl::Framebuffer::create(width, height);
    } else {
        m_framebuffer->resize(width, height);
    }
    m_context.viewportTexture = m_framebuffer->colorTexture();

    rb::Viewport& viewport = m_runtime.resource<rb::Viewport>();
    viewport.width = width;
    viewport.height = height;
    const rb::RenderView renderView = m_camera.renderView(viewport.aspect());
    m_runtime.resource<rb::RenderView>() = renderView;
    m_context.renderView = renderView;

    m_framebuffer->bind();
    // ImGui's GL backend mutates depth state each frame, so re-assert it for the
    // offscreen scene pass. The skybox is drawn first (scene over sky) rather than
    // last, which is robust across the offscreen target's depth handling.
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    m_device.setClearColor(glm::vec4(0.30f, 0.37f, 0.47f, 1.0f)); // fallback sky tint
    m_device.clear();
    m_runtime.tick(dt);
    rb::gl::Framebuffer::unbind();
}

void Editor::newScene() {
    m_runtime.scene().clear();
    m_context.selected = rb::Entity{};
    rb::log::info("editor: new (empty) scene");
}

void Editor::saveScene() {
    if (rb::SceneSerializer::saveToFile(m_runtime.scene(), m_registry, m_scenePath)) {
        rb::log::info("editor: saved scene to '{}'", m_scenePath);
    } else {
        rb::log::error("editor: failed to save scene to '{}'", m_scenePath);
    }
}

void Editor::openScene() {
    if (rb::SceneSerializer::loadFromFile(m_runtime.scene(), m_registry, m_scenePath)) {
        m_context.selected = rb::Entity{};
        rb::log::info("editor: loaded scene from '{}'", m_scenePath);
    } else {
        rb::log::warn("editor: could not load '{}'", m_scenePath);
    }
}

void Editor::startPlay() {
    // Snapshot the authored scene; gameplay edits during play are discarded on Stop.
    m_snapshot = rb::SceneSerializer::toJson(m_runtime.scene(), m_registry);
    m_runtime.beginPlay(); // fire onPlayBegin (scripts reset for a fresh session)
    m_playSession = true;
    m_paused = false;
    m_step = false;
    rb::log::info("editor: play");
}

void Editor::stopPlay() {
    m_playSession = false;
    m_paused = false;
    m_step = false;
    m_runtime.setPlaying(false);
    m_runtime.endPlay(); // fire onPlayEnd while entities still exist (scripts tear down)
    m_runtime.scene().clear();
    rb::SceneSerializer::fromJson(m_snapshot, m_runtime.scene(), m_registry);
    m_context.selected = rb::Entity{};
    rb::log::info("editor: stop (scene restored)");
}

void Editor::run() {
    buildDefaultScene();
    // Input lives as a resource so the ScriptSystem (and any gameplay system) can query it.
    rb::Input& input = m_runtime.addResource<rb::Input>(m_window.handle());
    m_runtime.start();

    rb::FrameClock clock;
    while (!m_window.shouldClose()) {
        m_window.pollEvents();
        input.update();

        // Latch free-look on a right-click inside the viewport, holding until the
        // button is released. Latching (rather than re-testing hover every frame)
        // keeps the camera active even though capturing the cursor stops ImGui from
        // reporting the viewport as hovered.
        if (!m_cameraActive) {
            if (m_context.viewportHovered && input.mousePressed(rb::MouseButton::Right)) {
                m_cameraActive = true;
                m_window.setCursorCaptured(true);
            }
        } else if (!input.mouseDown(rb::MouseButton::Right)) {
            m_cameraActive = false;
            m_window.setCursorCaptured(false);
        }

        ImGuiIO& io = ImGui::GetIO();
        if (m_cameraActive) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse; // ignore the captured cursor in the UI
        } else {
            io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }
        if (input.keyPressed(rb::Key::Escape) && !io.WantCaptureKeyboard && !m_cameraActive) {
            m_window.requestClose();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        drawDockspaceAndMenu();
        m_panels.render();

        const float dt = clock.tick();
        m_camera.update(input, dt, m_cameraActive);

        // Gate Play-phase systems: run them while playing and not paused, or for a
        // single Step. Always systems (render) tick regardless.
        m_runtime.setPlaying(m_playSession && (!m_paused || m_step));
        renderScene(std::max(1, m_context.viewportWidth), std::max(1, m_context.viewportHeight), dt);
        m_step = false;

        // Service a viewport pick now that world matrices and the framebuffer are
        // current (an invalid result clears the selection).
        if (m_context.pickRequested) {
            m_context.pickRequested = false;
            if (m_renderSystem != nullptr) {
                m_context.selected =
                    m_renderSystem->pick(m_runtime, m_context.pickX, m_context.pickY);
            }
        }


        m_device.setViewport(m_window.width(), m_window.height());
        m_device.setClearColor(glm::vec4(0.06f, 0.06f, 0.07f, 1.0f));
        m_device.clear();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        m_window.swapBuffers();
    }

    m_runtime.stop();
}

} // namespace rb::editor

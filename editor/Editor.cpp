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
#include "rabbet/audio/AudioAssetResolveSystem.h"
#include "rabbet/audio/AudioSystem.h"
#include "rabbet/core/Clock.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/physics/PhysicsSystem.h"
#include "rabbet/render/AssetResolveSystem.h"
#include "rabbet/render/BuiltinShaders.h"
#include "rabbet/render/DebugDraw.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/MaterialAssetResolveSystem.h"
#include "rabbet/render/MaterialComponent.h"
#include "rabbet/render/Image.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/render/Lighting.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelImport.h"
#include "rabbet/render/ModelRenderer.h"
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
#include "rabbet/serialize/PrefabAssetResolveSystem.h"
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

#ifndef RB_EDITOR_WORKSPACE
#define RB_EDITOR_WORKSPACE "workspace"
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
    // Keep ImGui's layout file in a dedicated workspace dir (absolute path) so it never litters
    // the launch directory — uses a dedicated workspace. ImGui keeps the pointer, so the
    // path string must outlive the context (static).
    std::filesystem::create_directories(RB_EDITOR_WORKSPACE);
    static const std::string iniPath =
        (std::filesystem::path(RB_EDITOR_WORKSPACE) / "forge.ini").string();
    io.IniFilename = iniPath.c_str();
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
    // Register the built-in shaders + default PBR material so the crate's MaterialComponent
    // resolves to a real asset (the data-driven path) without depending on any asset file.
    rb::registerDefaultRenderAssets(assets);
    rb::AssetDatabase& database = m_runtime.addResource<rb::AssetDatabase>();
    rb::Lighting& lighting = m_runtime.addResource<rb::Lighting>();
    lighting.ambient = glm::vec3(0.32f, 0.34f, 0.40f);
    m_runtime.addResource<rb::DebugDraw>(rb::DebugDraw{false}); // collider wireframes off — clean scene

    // ScriptSystem (Play phase) runs before TransformSystem so a script's transform edits
    // are baked into world matrices the same frame; its resolve system (Always) keeps the
    // .lua handle current and polls for hot reloads.
    m_runtime.addSystem<rb::ScriptAssetResolveSystem>();
    m_runtime.addSystem<rb::AudioAssetResolveSystem>();
    m_runtime.addSystem<rb::MaterialAssetResolveSystem>();
    m_runtime.addSystem<rb::PrefabAssetResolveSystem>();
    m_runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    m_runtime.addSystem<rb::PhysicsSystem, rb::SystemPhase::Play>();
    m_runtime.addSystem<rb::AudioSystem, rb::SystemPhase::Play>();
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

    // A single hero asset on the skybox — the wooden crate, a real PBR model (Poly Haven, CC0)
    // loaded through the asset pipeline (assimp -> AssetManager) and referenced by uuid via
    // ModelRenderer. The default scene is kept deliberately uncluttered for a clean first launch.
    const rb::AssetHandle<rb::ModelAsset> crate = rb::loadModelAsset(
        assets, assetsRoot / "models/wooden_crate_02/wooden_crate_02_1k.gltf", {0.0f, 0.7f});
    const rb::Entity heroEntity = scene.create();
    scene.add<rb::Name>(heroEntity, rb::Name{"Crate"});
    rb::Transform heroTransform;
    heroTransform.position = glm::vec3(0.0f, -0.4f, 0.0f);
    heroTransform.rotation = glm::angleAxis(glm::radians(28.0f), glm::vec3(0.0f, 1.0f, 0.0f));
    heroTransform.scale = glm::vec3(3.0f);
    scene.add<rb::Transform>(heroEntity, heroTransform);
    rb::ModelRenderer renderer;
    renderer.model = assets.uuidOf(crate);
    renderer.handle = crate;
    scene.add<rb::ModelRenderer>(heroEntity, renderer);

    // Shade the crate through the data-driven path: a MaterialComponent bound to the built-in
    // default PBR material. With no uniform overrides it renders identically to the built-in
    // model path, while the inspector now exposes the shader's reflected uniforms to edit.
    scene.add<rb::MaterialComponent>(heroEntity,
                                     rb::MaterialComponent{rb::builtin::kDefaultMaterial, {}});
    m_context.selected = heroEntity;

    // A sample Lua behavior: pressing Play slowly spins the hero (and hot-reloads on .lua edits).
    const rb::AssetHandle<rb::ScriptAsset> spin =
        rb::loadScriptAsset(assets, assetsRoot / "scripts/spin.lua");
    if (spin.valid()) {
        rb::ScriptComponent script;
        script.script = assets.uuidOf(spin);
        script.handle = spin;
        if (const rb::ScriptAsset* asset = assets.get<rb::ScriptAsset>(spin)) {
            rb::introspectScriptFields(asset->source, script.fields);
        }
        scene.add<rb::ScriptComponent>(heroEntity, script);
    }

    // One warm key light so the PBR crate reads cleanly against the sky.
    const rb::Entity sun = scene.create();
    scene.add<rb::Name>(sun, rb::Name{"Sun"});
    rb::DirectionalLight sunLight;
    sunLight.direction = glm::normalize(glm::vec3(-0.55f, -0.85f, -0.5f));
    sunLight.color = glm::vec3(1.0f, 0.96f, 0.88f);
    sunLight.intensity = 2.8f;
    scene.add<rb::DirectionalLight>(sun, sunLight);

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

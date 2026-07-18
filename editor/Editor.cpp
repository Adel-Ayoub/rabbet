#include "editor/Editor.h"

#include "editor/ComponentDrawers.h"
#include "editor/Icons.h"
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
#include "rabbet/scene/CameraView.h"
#include "rabbet/render/EnvironmentLighting.h"
#include "rabbet/render/Skybox.h"
#include "rabbet/render/SkyboxSystem.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/render/gl/Cubemap.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/particle/ParticleSystem.h"
#include "rabbet/terrain/TerrainSystem.h"
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

#include <GLFW/glfw3.h>

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
#include <cstdio>
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
constexpr float kStatusBarHeight = 24.0f;

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
    // The default scratch scene lives in the workspace too, so saving never litters the launch dir.
    m_scenePath = (std::filesystem::path(RB_EDITOR_WORKSPACE) / "rabbet_editor.scene.json").string();
    applyTheme();
    // Rasterize fonts at the display's content scale so text is crisp on hidpi.
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    glfwGetWindowContentScale(m_window.handle(), &scaleX, &scaleY);
    loadFonts(std::max(scaleX, scaleY));
    ImGui_ImplGlfw_InitForOpenGL(m_window.handle(), true);
    ImGui_ImplOpenGL3_Init("#version 410");

    m_context.thumbnails = &m_thumbnails;

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
    m_runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>(&m_context.registry);
    m_runtime.addSystem<rb::PhysicsSystem, rb::SystemPhase::Play>();
    m_runtime.addSystem<rb::AudioSystem, rb::SystemPhase::Play>();
    m_runtime.addSystem<rb::TransformSystem>();
    m_runtime.addSystem<rb::LightSystem>();
    m_runtime.addSystem<rb::AssetResolveSystem>();
    // ParticleSystem is Always (emitters preview without Play) and runs after AssetResolveSystem so
    // a sprite handle is resolved before it publishes, and before RenderSystem consumes it.
    m_runtime.addSystem<rb::ParticleSystem>();
    // TerrainSystem is Always too (terrain previews in edit mode) and runs after AssetResolveSystem
    // so layer/splat handles resolve before it publishes, and before RenderSystem consumes them.
    m_runtime.addSystem<rb::TerrainSystem>();
    m_runtime.addSystem<rb::SkyboxSystem>();
    m_renderSystem = &m_runtime.addSystem<rb::RenderSystem>();

    const std::filesystem::path assetsRoot{RB_EDITOR_ASSETS};

    // Real sky: a cubemap skybox.
    m_runtime.addResource<rb::Skybox>(rb::Skybox{loadCubemap(assetsRoot / "skybox/sky"),
                                                 rb::gl::Mesh::create(rb::geometry::cube())});

    // Diffuse image-based ambient convolved from that skybox, off by default (the viewport
    // "Environment" toggle switches it on). Built once here while the GL context is current.
    m_runtime.addResource<rb::EnvironmentLight>(
        rb::buildEnvironmentLight(m_runtime.resource<rb::Skybox>().cubemap));

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
        drawTransport();
        ImGui::EndMenuBar();
    }

    const ImGuiID dockId = ImGui::GetID("RabbetDockSpace");
    if (ImGui::DockBuilderGetNode(dockId) == nullptr) {
        buildDefaultLayout(dockId);
    }
    // Leave a strip below the dockspace for the status bar.
    ImGui::DockSpace(dockId, ImVec2(0.0f, -kStatusBarHeight), ImGuiDockNodeFlags_None);

    drawStatusBar();

    ImGui::End();
}

// Play controls inline in the menu bar, centered in the window, so the chrome
// stays a single row.
void Editor::drawTransport() {
    const Palette& p = palette();
    const float buttonWidth = 30.0f;
    const float spacing = ImGui::GetStyle().ItemSpacing.x;
    const float stripWidth = 4.0f * buttonWidth + 3.0f * spacing;
    const ImVec2 buttonSize(buttonWidth, 0.0f);
    ImGui::SetCursorPosX(
        std::max(ImGui::GetCursorPosX(), (ImGui::GetWindowSize().x - stripWidth) * 0.5f));

    const bool playing = m_playSession && !m_paused;

    if (playing) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
    }
    if (ImGui::Button(icon::kPlay, buttonSize)) {
        if (!m_playSession) {
            startPlay();
        } else {
            m_paused = false;
        }
    }
    if (playing) {
        ImGui::PopStyleColor();
    }
    ImGui::SetItemTooltip(m_playSession ? "Resume" : "Play");

    ImGui::BeginDisabled(!m_playSession);
    // Snapshot before the click: the button flips m_paused, and push/pop must agree.
    const bool paused = m_paused;
    if (paused) {
        ImGui::PushStyleColor(ImGuiCol_Text, p.warn);
    }
    if (ImGui::Button(icon::kPause, buttonSize)) {
        m_paused = !m_paused;
    }
    if (paused) {
        ImGui::PopStyleColor();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Pause");

    ImGui::BeginDisabled(!(m_playSession && m_paused));
    if (ImGui::Button(icon::kStepForward, buttonSize)) {
        m_step = true;
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Step one frame");

    ImGui::BeginDisabled(!m_playSession);
    if (ImGui::Button(icon::kSquare, buttonSize)) {
        stopPlay();
    }
    ImGui::EndDisabled();
    ImGui::SetItemTooltip("Stop");
}

void Editor::drawStatusBar() {
    const Palette& p = palette();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, p.well);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 3.0f));
    ImGui::BeginChild("##status", ImVec2(0.0f, 0.0f), ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    const char* state = "Editing";
    ImVec4 stateColor = p.inkMuted;
    if (m_playSession) {
        state = m_paused ? "Paused" : "Playing";
        stateColor = m_paused ? p.warn : p.good;
    }
    const float lineHeight = ImGui::GetTextLineHeight();
    const ImVec2 dotPos = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddCircleFilled(
        ImVec2(dotPos.x + 4.0f, dotPos.y + lineHeight * 0.5f + 1.0f), 3.5f,
        ImGui::GetColorU32(stateColor));
    ImGui::Dummy(ImVec2(12.0f, lineHeight));
    ImGui::SameLine();
    ImGui::TextUnformatted(state);

    std::size_t assetCount = 0;
    if (const rb::AssetDatabase* database = m_runtime.tryResource<rb::AssetDatabase>()) {
        assetCount = database->size();
    }
    char stats[96];
    std::snprintf(stats, sizeof(stats), "%.0f fps    %zu entities    %zu assets",
                  static_cast<double>(ImGui::GetIO().Framerate), m_runtime.scene().aliveCount(),
                  assetCount);
    ImGui::PushFont(fonts().mono);
    const float statsWidth = ImGui::CalcTextSize(stats).x;
    ImGui::SameLine(ImGui::GetWindowSize().x - statsWidth - 10.0f);
    ImGui::PushStyleColor(ImGuiCol_Text, p.inkMuted);
    ImGui::TextUnformatted(stats);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void Editor::renderScene(int width, int height, float dt) {
    // An enabled PostProcess switches the scene target to linear HDR (RGBA16F) so the post chain can
    // tone-map; without one the scene renders straight to an LDR target exactly as before.
    rb::PostProcess* post = rb::activePostProcess(m_runtime.scene());
    const bool hdr = post != nullptr;
    if (!m_framebuffer.has_value() || m_framebufferHdr != hdr) {
        m_framebuffer = rb::gl::Framebuffer::create(width, height, hdr);
        m_framebufferHdr = hdr;
    } else {
        m_framebuffer->resize(width, height);
    }

    rb::Viewport& viewport = m_runtime.resource<rb::Viewport>();
    viewport.width = width;
    viewport.height = height;
    rb::RenderView renderView = m_camera.renderView(viewport.aspect());
    if (m_runtime.inPlaySession()) {
        // The scene's own camera drives the Play view (Pause keeps it); Stop resumes the
        // editor camera. A scene with no camera keeps the editor view even while playing.
        if (const std::optional<rb::RenderView> gameView =
                rb::activeCameraView(m_runtime.scene(), viewport.aspect())) {
            renderView = *gameView;
        }
    }
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

    // Scripts inside that tick can spawn or destroy entities, which reallocates or
    // swap-removes the PostProcess pool; the pre-tick pointer may dangle, so re-query.
    // A post component destroyed mid-tick degrades to the raw frame for one frame.
    post = rb::activePostProcess(m_runtime.scene());
    m_context.viewportTexture =
        hdr && post != nullptr
            ? m_postProcessor.process(m_framebuffer->colorTexture(), width, height, *post)
            : m_framebuffer->colorTexture();
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
    m_postProcessor.init(); // compile the post shaders now that a GL context is current
    m_thumbnails.init();    // compile the preview shader + build preview geometry

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
        m_thumbnails.beginFrame(); // cap new asset previews rendered this frame
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
        const ImVec4& chrome = palette().well;
        m_device.setClearColor(glm::vec4(chrome.x, chrome.y, chrome.z, 1.0f));
        m_device.clear();
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        m_window.swapBuffers();
    }

    m_runtime.stop();
}

} // namespace rb::editor

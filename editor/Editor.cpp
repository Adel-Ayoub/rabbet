#include "editor/Editor.h"

#include "editor/Palette.h"
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
#include "rabbet/render/AssetResolveSystem.h"
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
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>

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

    m_runtime.addSystem<rb::TransformSystem>();
    m_runtime.addSystem<rb::LightSystem>();
    m_runtime.addSystem<rb::AssetResolveSystem>();
    m_runtime.addSystem<rb::SkyboxSystem>();
    m_runtime.addSystem<rb::RenderSystem>();

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
    m_runtime.resource<rb::RenderView>() = m_camera.renderView(viewport.aspect());

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

void Editor::run() {
    buildDefaultScene();
    m_runtime.start();

    rb::Input input(m_window.handle());
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

        drawDockspaceAndMenu();
        m_panels.render();

        const float dt = clock.tick();
        m_camera.update(input, dt, m_cameraActive);

        renderScene(std::max(1, m_context.viewportWidth), std::max(1, m_context.viewportHeight), dt);

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

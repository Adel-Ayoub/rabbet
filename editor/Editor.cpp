#include "editor/Editor.h"

#include "rabbet/core/Clock.h"
#include "rabbet/platform/Input.h"
#include "rabbet/platform/Window.h"
#include "rabbet/render/Geometry.h"
#include "rabbet/render/Material.h"
#include "rabbet/render/PbrMaterial.h"
#include "rabbet/render/RenderDevice.h"
#include "rabbet/render/RenderSystem.h"
#include "rabbet/render/Viewport.h"
#include "rabbet/render/gl/Mesh.h"
#include "rabbet/render/gl/Texture.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/CameraSystem.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/LightSystem.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/util/Log.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace forge {

Editor::Editor(rb::Window& window, rb::RenderDevice& device) : m_window(window), m_device(device) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(m_window.handle(), true);
    ImGui_ImplOpenGL3_Init("#version 410");
}

Editor::~Editor() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void Editor::addSphere(const std::string& name, const glm::vec3& position, const glm::vec3& color,
                       float metallic, float roughness) {
    rb::Scene& scene = m_runtime.scene();
    const rb::Entity entity = scene.create();
    rb::Transform transform;
    transform.position = position;
    scene.add<rb::Transform>(entity, transform);
    scene.add<rb::gl::Mesh>(entity, rb::gl::Mesh::create(rb::geometry::sphere()));
    scene.add<rb::PbrMaterial>(
        entity, rb::PbrMaterial{rb::gl::Texture::solid(255, 255, 255), color, metallic, roughness, 1.0f});
    m_entities.emplace_back(name, entity);
}

void Editor::buildScene() {
    m_runtime.addResource<rb::Viewport>();
    m_runtime.addSystem<rb::TransformSystem>();
    m_runtime.addSystem<rb::CameraSystem>();
    m_runtime.addSystem<rb::LightSystem>();
    m_runtime.addSystem<rb::RenderSystem>();

    rb::Scene& scene = m_runtime.scene();

    const rb::Entity camera = scene.create();
    rb::Transform cameraTransform;
    cameraTransform.position = glm::vec3(0.0f, 1.5f, 5.5f);
    cameraTransform.rotation = glm::angleAxis(glm::radians(-12.0f), glm::vec3(1.0f, 0.0f, 0.0f));
    scene.add<rb::Transform>(camera, cameraTransform);
    scene.add<rb::Camera>(camera, rb::Camera{});
    m_entities.emplace_back("Camera", camera);

    const rb::Entity floor = scene.create();
    rb::Transform floorTransform;
    floorTransform.position = glm::vec3(0.0f, -1.0f, 0.0f);
    floorTransform.scale = glm::vec3(14.0f, 0.3f, 14.0f);
    scene.add<rb::Transform>(floor, floorTransform);
    scene.add<rb::gl::Mesh>(floor, rb::gl::Mesh::create(rb::geometry::cube()));
    scene.add<rb::Material>(
        floor, rb::Material{rb::gl::Texture::solid(170, 170, 175), glm::vec3(0.85f), 0.1f, 8.0f});
    m_entities.emplace_back("Floor", floor);

    const rb::Entity cube = scene.create();
    rb::Transform cubeTransform;
    cubeTransform.position = glm::vec3(-1.7f, 0.0f, 0.0f);
    scene.add<rb::Transform>(cube, cubeTransform);
    scene.add<rb::gl::Mesh>(cube, rb::gl::Mesh::create(rb::geometry::cube()));
    scene.add<rb::Material>(
        cube, rb::Material{rb::gl::Texture::solid(210, 120, 70), glm::vec3(1.0f), 0.5f, 32.0f});
    m_entities.emplace_back("Cube", cube);

    addSphere("Metal Sphere", glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.78f, 0.34f), 1.0f, 0.25f);
    addSphere("Rough Sphere", glm::vec3(1.7f, 0.0f, 0.0f), glm::vec3(0.9f, 0.25f, 0.25f), 0.0f, 0.55f);

    const rb::Entity sun = scene.create();
    rb::DirectionalLight sunLight;
    sunLight.direction = glm::normalize(glm::vec3(-0.5f, -0.9f, -0.6f));
    sunLight.color = glm::vec3(1.0f, 0.95f, 0.85f);
    sunLight.intensity = 2.5f;
    scene.add<rb::DirectionalLight>(sun, sunLight);
    m_entities.emplace_back("Sun", sun);

    const rb::Entity lamp = scene.create();
    rb::Transform lampTransform;
    lampTransform.position = glm::vec3(2.0f, 2.0f, 2.5f);
    scene.add<rb::Transform>(lamp, lampTransform);
    rb::PointLight lampLight;
    lampLight.color = glm::vec3(0.5f, 0.7f, 1.0f);
    lampLight.intensity = 6.0f;
    scene.add<rb::PointLight>(lamp, lampLight);
    m_entities.emplace_back("Lamp", lamp);

    m_selected = cube;
}

void Editor::drawHierarchy() {
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(220.0f, 280.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Hierarchy");
    for (const auto& [name, entity] : m_entities) {
        if (ImGui::Selectable(name.c_str(), entity == m_selected)) {
            m_selected = entity;
        }
    }
    ImGui::End();
}

void Editor::drawInspector() {
    ImGui::SetNextWindowPos(ImVec2(20.0f, 320.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300.0f, 360.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");

    rb::Scene& scene = m_runtime.scene();
    if (!scene.alive(m_selected)) {
        ImGui::TextUnformatted("Nothing selected");
        ImGui::End();
        return;
    }

    if (rb::Transform* transform = scene.tryGet<rb::Transform>(m_selected)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Position", &transform->position.x, 0.05f);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(transform->rotation));
            if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) {
                transform->rotation = glm::quat(glm::radians(euler));
            }
            ImGui::DragFloat3("Scale", &transform->scale.x, 0.05f, 0.01f, 100.0f);
        }
    }
    if (rb::DirectionalLight* light = scene.tryGet<rb::DirectionalLight>(m_selected)) {
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Color", &light->color.x);
            ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 20.0f);
            ImGui::DragFloat3("Direction", &light->direction.x, 0.02f);
        }
    }
    if (rb::PointLight* light = scene.tryGet<rb::PointLight>(m_selected)) {
        if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Color", &light->color.x);
            ImGui::DragFloat("Intensity", &light->intensity, 0.05f, 0.0f, 50.0f);
        }
    }
    if (rb::Material* material = scene.tryGet<rb::Material>(m_selected)) {
        if (ImGui::CollapsingHeader("Material (Phong)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Tint", &material->tint.x);
            ImGui::DragFloat("Specular", &material->specular, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Shininess", &material->shininess, 0.5f, 1.0f, 256.0f);
        }
    }
    if (rb::PbrMaterial* material = scene.tryGet<rb::PbrMaterial>(m_selected)) {
        if (ImGui::CollapsingHeader("Material (PBR)", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Base Color", &material->baseColor.x);
            ImGui::DragFloat("Metallic", &material->metallic, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Roughness", &material->roughness, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Ambient Occlusion", &material->ao, 0.01f, 0.0f, 1.0f);
        }
    }
    ImGui::End();
}

void Editor::drawUi() {
    drawHierarchy();
    drawInspector();
}

void Editor::run() {
    buildScene();
    m_runtime.start();

    rb::Input input(m_window.handle());
    rb::FrameClock clock;
    while (!m_window.shouldClose()) {
        m_window.pollEvents();
        input.update();
        if (input.keyPressed(rb::Key::Escape) && !ImGui::GetIO().WantCaptureKeyboard) {
            m_window.requestClose();
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        drawUi();

        rb::Viewport& viewport = m_runtime.resource<rb::Viewport>();
        viewport.width = m_window.width();
        viewport.height = m_window.height();

        m_device.setViewport(viewport.width, viewport.height);
        m_device.setClearColor(glm::vec4(0.10f, 0.11f, 0.13f, 1.0f));
        m_device.clear();
        m_runtime.tick(clock.tick());

        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        m_window.swapBuffers();
    }
    m_runtime.stop();
}

} // namespace forge

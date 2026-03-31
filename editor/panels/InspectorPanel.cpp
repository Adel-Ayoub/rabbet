#include "editor/panels/InspectorPanel.h"

#include "editor/EditorContext.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"

#include <glm/gtc/quaternion.hpp>
#include <imgui.h>

#include <cstdio>

namespace rb::editor {
namespace {
constexpr const char* kShapes[] = {"Cube", "Sphere", "Plane"};
}

void InspectorPanel::onImGui() {
    rb::Scene& scene = m_context.runtime.scene();
    if (!scene.alive(m_context.selected)) {
        ImGui::TextDisabled("Nothing selected");
        return;
    }
    const rb::Entity e = m_context.selected;

    if (rb::Name* n = scene.tryGet<rb::Name>(e)) {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer), "%s", n->value.c_str());
        if (ImGui::InputText("Name", buffer, sizeof(buffer))) {
            n->value = buffer;
        }
        ImGui::Separator();
    }

    if (rb::Transform* t = scene.tryGet<rb::Transform>(e)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Position", &t->position.x, 0.05f);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(t->rotation));
            if (ImGui::DragFloat3("Rotation", &euler.x, 0.5f)) {
                t->rotation = glm::quat(glm::radians(euler));
            }
            ImGui::DragFloat3("Scale", &t->scale.x, 0.05f, 0.01f, 100.0f);
        }
    }

    if (rb::Primitive* p = scene.tryGet<rb::Primitive>(e)) {
        if (ImGui::CollapsingHeader("Primitive", ImGuiTreeNodeFlags_DefaultOpen)) {
            int shape = static_cast<int>(p->shape);
            if (ImGui::Combo("Shape", &shape, kShapes, 3)) {
                p->shape = static_cast<rb::PrimitiveShape>(shape);
            }
            ImGui::ColorEdit3("Color", &p->color.x);
            ImGui::DragFloat("Metallic", &p->metallic, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Roughness", &p->roughness, 0.01f, 0.0f, 1.0f);
        }
    }

    if (rb::DirectionalLight* l = scene.tryGet<rb::DirectionalLight>(e)) {
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Color", &l->color.x);
            ImGui::DragFloat("Intensity", &l->intensity, 0.05f, 0.0f, 20.0f);
            ImGui::DragFloat3("Direction", &l->direction.x, 0.02f);
        }
    }

    if (rb::PointLight* l = scene.tryGet<rb::PointLight>(e)) {
        if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Color", &l->color.x);
            ImGui::DragFloat("Intensity", &l->intensity, 0.05f, 0.0f, 50.0f);
        }
    }
}

} // namespace rb::editor

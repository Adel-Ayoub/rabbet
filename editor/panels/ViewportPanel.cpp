#include "editor/panels/ViewportPanel.h"

#include "editor/EditorContext.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/DebugDraw.h"
#include "rabbet/render/EnvironmentLighting.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/scene/Transform.h"

#include <ImGuizmo.h>
#include <imgui.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/matrix_decompose.hpp>

#include <algorithm>

namespace rb::editor {

void ViewportPanel::draw() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(name(), &openRef());
    ImGui::PopStyleVar();
    if (visible) {
        onImGui();
    }
    ImGui::End();
}

void ViewportPanel::onImGui() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 imageMin = ImGui::GetCursorScreenPos();
    m_context.viewportWidth = std::max(1, static_cast<int>(avail.x));
    m_context.viewportHeight = std::max(1, static_cast<int>(avail.y));
    m_context.viewportHovered = ImGui::IsWindowHovered();
    m_context.viewportFocused = ImGui::IsWindowFocused();

    if (m_context.viewportTexture != 0 && avail.x > 0.0f && avail.y > 0.0f) {
        // Flip V: GL textures have their origin at the bottom-left.
        const ImTextureID tex = static_cast<ImTextureID>(m_context.viewportTexture);
        ImGui::Image(tex, avail, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
        drawGizmo(imageMin, avail);

        // A left-click on the image (not on the tool row or the gizmo) requests a pick.
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool inImage = mouse.x >= imageMin.x && mouse.y >= imageMin.y &&
                             mouse.x < imageMin.x + avail.x && mouse.y < imageMin.y + avail.y;
        if (inImage && ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() &&
            !ImGuizmo::IsOver() && !ImGuizmo::IsUsing() &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            m_context.pickRequested = true;
            m_context.pickX = static_cast<int>(mouse.x - imageMin.x);
            m_context.pickY = static_cast<int>(mouse.y - imageMin.y);
        }
    }
}

void ViewportPanel::drawGizmo(const ImVec2& imageMin, const ImVec2& imageSize) {
    // Tool row overlaid at the viewport's top-left corner.
    ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
    ImGui::BeginGroup();
    if (ImGui::Button("Move")) {
        m_gizmoOp = ImGuizmo::TRANSLATE;
    }
    ImGui::SameLine();
    if (ImGui::Button("Rotate")) {
        m_gizmoOp = ImGuizmo::ROTATE;
    }
    ImGui::SameLine();
    if (ImGui::Button("Scale")) {
        m_gizmoOp = ImGuizmo::SCALE;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_gizmoMode == ImGuizmo::WORLD ? "World" : "Local")) {
        m_gizmoMode = (m_gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    }
    ImGui::SameLine();
    ImGui::Checkbox("Snap", &m_snap);
    if (rb::DebugDraw* debug = m_context.runtime.tryResource<rb::DebugDraw>()) {
        ImGui::SameLine();
        ImGui::Checkbox("Colliders", &debug->colliders);
    }
    if (rb::EnvironmentLight* env = m_context.runtime.tryResource<rb::EnvironmentLight>()) {
        ImGui::SameLine();
        ImGui::Checkbox("Environment", &env->enabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Light ambient from the skybox (diffuse image-based lighting).");
        }
    }
    // Quick toggle for the scene's post-processing (the first PostProcess component), for A/B
    // comparison without leaving the viewport. Editing the full settings lives in the inspector.
    rb::PostProcess* post = nullptr;
    m_context.runtime.scene().each<rb::PostProcess>([&post](rb::Entity, rb::PostProcess& p) {
        if (post == nullptr) {
            post = &p;
        }
    });
    if (post != nullptr) {
        ImGui::SameLine();
        ImGui::Checkbox("Post", &post->enabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("HDR tone-mapping, bloom, colour grade, and FXAA.");
        }
    }
    ImGui::EndGroup();

    rb::Scene& scene = m_context.runtime.scene();
    if (!scene.alive(m_context.selected)) {
        return;
    }
    rb::Transform* transform = scene.tryGet<rb::Transform>(m_context.selected);
    if (transform == nullptr) {
        return;
    }

    ImGuizmo::SetOrthographic(false);
    ImGuizmo::SetDrawlist();
    ImGuizmo::SetRect(imageMin.x, imageMin.y, imageSize.x, imageSize.y);

    float snapValue = m_snapTranslate;
    if (m_gizmoOp == ImGuizmo::ROTATE) {
        snapValue = m_snapRotate;
    } else if (m_gizmoOp == ImGuizmo::SCALE) {
        snapValue = m_snapScale;
    }
    const float snapVec[3] = {snapValue, snapValue, snapValue};

    glm::mat4 model = transform->matrix();
    if (ImGuizmo::Manipulate(glm::value_ptr(m_context.renderView.view),
                             glm::value_ptr(m_context.renderView.projection), m_gizmoOp, m_gizmoMode,
                             glm::value_ptr(model), nullptr, m_snap ? snapVec : nullptr)) {
        glm::vec3 translation;
        glm::vec3 scale;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::quat rotation;
        if (glm::decompose(model, scale, rotation, translation, skew, perspective)) {
            transform->position = translation;
            transform->rotation = rotation;
            transform->scale = scale;
        }
    }
}

} // namespace rb::editor

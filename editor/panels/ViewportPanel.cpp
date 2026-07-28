#include "editor/panels/ViewportPanel.h"

#include "editor/EditorContext.h"
#include "editor/Icons.h"
#include "editor/Palette.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/DebugDraw.h"
#include "rabbet/render/EnvironmentLighting.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/scene/Hierarchy.h"
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

    // Gizmo shortcuts while the cursor is over the scene. Suppressed during a play
    // session so gameplay input (WASD scripts) never doubles as a tool switch, and
    // while typing. Free-look capture drops the hover flag, so camera keys are safe.
    if (m_context.viewportHovered && !ImGui::GetIO().WantTextInput &&
        !m_context.runtime.inPlaySession()) {
        if (ImGui::IsKeyPressed(ImGuiKey_Q, false)) {
            m_gizmoMode = (m_gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_W, false)) {
            m_gizmoOp = ImGuizmo::TRANSLATE;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_E, false)) {
            m_gizmoOp = ImGuizmo::ROTATE;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_R, false)) {
            m_gizmoOp = ImGuizmo::SCALE;
        }
    }

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
    const Palette& p = palette();

    // Tool row overlaid at the viewport's top-left corner: translucent icon
    // buttons, filled while a mode is active, accent-lit while a toggle is on.
    ImGui::SetCursorScreenPos(ImVec2(imageMin.x + 8.0f, imageMin.y + 8.0f));
    ImGui::PushStyleColor(ImGuiCol_Button, withAlpha(p.base, 0.80f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, withAlpha(p.lift, 0.90f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, withAlpha(p.press, 0.95f));
    ImGui::BeginGroup();

    const ImVec2 toolSize(26.0f, 22.0f);
    const auto tool = [&](const char* glyph, bool active, const char* tip) {
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, p.accentDim);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, p.accentDim);
        }
        const bool clicked = ImGui::Button(glyph, toolSize);
        if (active) {
            ImGui::PopStyleColor(2);
        }
        ImGui::SetItemTooltip("%s", tip);
        return clicked;
    };
    const auto toggle = [&](const char* glyph, bool value, const char* tip) {
        if (value) {
            ImGui::PushStyleColor(ImGuiCol_Text, p.accent);
        }
        const bool clicked = ImGui::Button(glyph, toolSize);
        if (value) {
            ImGui::PopStyleColor();
        }
        ImGui::SetItemTooltip("%s", tip);
        return clicked;
    };

    if (tool(icon::kMove3d, m_gizmoOp == ImGuizmo::TRANSLATE, "Move (W)")) {
        m_gizmoOp = ImGuizmo::TRANSLATE;
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (tool(icon::kRotate3d, m_gizmoOp == ImGuizmo::ROTATE, "Rotate (E)")) {
        m_gizmoOp = ImGuizmo::ROTATE;
    }
    ImGui::SameLine(0.0f, 4.0f);
    if (tool(icon::kScale3d, m_gizmoOp == ImGuizmo::SCALE, "Scale (R)")) {
        m_gizmoOp = ImGuizmo::SCALE;
    }

    ImGui::SameLine(0.0f, 12.0f);
    if (ImGui::Button(m_gizmoMode == ImGuizmo::WORLD ? "World" : "Local",
                      ImVec2(0.0f, toolSize.y))) {
        m_gizmoMode = (m_gizmoMode == ImGuizmo::LOCAL) ? ImGuizmo::WORLD : ImGuizmo::LOCAL;
    }
    ImGui::SetItemTooltip("Gizmo space (Q)");
    ImGui::SameLine(0.0f, 4.0f);
    if (toggle(icon::kMagnet, m_snap, "Snap")) {
        m_snap = !m_snap;
    }
    ImGui::SameLine(0.0f, 12.0f);
    if (toggle(icon::kGrid, m_context.showGrid, "Ground grid")) {
        m_context.showGrid = !m_context.showGrid;
    }

    if (rb::DebugDraw* debug = m_context.runtime.tryResource<rb::DebugDraw>()) {
        ImGui::SameLine(0.0f, 12.0f);
        if (toggle(icon::kSquareDashed, debug->colliders, "Collider wireframes")) {
            debug->colliders = !debug->colliders;
        }
    }
    if (rb::EnvironmentLight* env = m_context.runtime.tryResource<rb::EnvironmentLight>()) {
        ImGui::SameLine(0.0f, 4.0f);
        if (toggle(icon::kGlobe, env->enabled,
                   "Light ambient from the skybox (image-based lighting)")) {
            env->enabled = !env->enabled;
        }
    }
    // Quick toggle for the scene's post-processing (the first PostProcess component), for A/B
    // comparison without leaving the viewport. Editing the full settings lives in the inspector.
    rb::PostProcess* post = nullptr;
    m_context.runtime.scene().each<rb::PostProcess>([&post](rb::Entity, rb::PostProcess& pp) {
        if (post == nullptr) {
            post = &pp;
        }
    });
    if (post != nullptr) {
        ImGui::SameLine(0.0f, 4.0f);
        if (toggle(icon::kWandSparkles, post->enabled,
                   "Post-processing (tonemap, bloom, grade, FXAA)")) {
            post->enabled = !post->enabled;
        }
    }
    ImGui::EndGroup();
    ImGui::PopStyleColor(3);

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

    // The gizmo manipulates the world matrix; a parented selection converts the result
    // back into its local frame before the decompose (a root passes straight through).
    const rb::Entity parent = rb::parentOf(scene, m_context.selected);
    const glm::mat4 parentWorld =
        parent.valid() ? rb::worldMatrixOf(scene, parent) : glm::mat4(1.0f);
    glm::mat4 model = parentWorld * transform->matrix();
    if (ImGuizmo::Manipulate(glm::value_ptr(m_context.renderView.view),
                             glm::value_ptr(m_context.renderView.projection), m_gizmoOp, m_gizmoMode,
                             glm::value_ptr(model), nullptr, m_snap ? snapVec : nullptr)) {
        const glm::mat4 local = parent.valid() ? glm::inverse(parentWorld) * model : model;
        glm::vec3 translation;
        glm::vec3 scale;
        glm::vec3 skew;
        glm::vec4 perspective;
        glm::quat rotation;
        if (glm::decompose(local, scale, rotation, translation, skew, perspective)) {
            transform->position = translation;
            transform->rotation = rotation;
            transform->scale = scale;
        }
    }
}

} // namespace rb::editor

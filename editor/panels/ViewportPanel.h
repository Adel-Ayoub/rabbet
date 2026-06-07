#pragma once

#include "editor/Panel.h"

#include <ImGuizmo.h>

namespace rb::editor {

// Shows the scene rendered into the editor's offscreen framebuffer as an image,
// reports its desired render size and hover/focus state back to the EditorContext,
// and overlays an ImGuizmo transform gizmo for the current selection.
class ViewportPanel final : public Panel {
public:
    explicit ViewportPanel(EditorContext& context) : Panel(context) {}

    [[nodiscard]] const char* name() const override { return "Viewport"; }
    void onImGui() override;
    void draw() override;

private:
    void drawGizmo(const ImVec2& imageMin, const ImVec2& imageSize);

    ImGuizmo::OPERATION m_gizmoOp = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE m_gizmoMode = ImGuizmo::LOCAL;
    bool m_snap = false;
    float m_snapTranslate = 0.5f;
    float m_snapRotate = 15.0f;
    float m_snapScale = 0.25f;
};

} // namespace rb::editor

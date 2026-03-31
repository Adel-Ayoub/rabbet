#pragma once

#include "editor/Panel.h"

namespace rb::editor {

// Shows the scene rendered into the editor's offscreen framebuffer as an image,
// reports its desired render size and hover/focus state back to the EditorContext,
// and draws with zero padding so the image fills the dock node.
class ViewportPanel final : public Panel {
public:
    explicit ViewportPanel(EditorContext& context) : Panel(context) {}

    [[nodiscard]] const char* name() const override { return "Viewport"; }
    void onImGui() override;
    void draw() override;
};

} // namespace rb::editor

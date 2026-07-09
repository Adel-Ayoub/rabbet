#pragma once

#include "editor/Panel.h"

namespace rb::editor {

// Shows the scene as a parent/child tree and supports create (empty / primitives /
// lights), subtree duplicate and delete, and drag-to-reparent (a drop below the tree
// un-parents). Selection is written back into the EditorContext.
class HierarchyPanel final : public Panel {
public:
    explicit HierarchyPanel(EditorContext& context) : Panel(context) {}

    [[nodiscard]] const char* name() const override { return "Hierarchy"; }
    void onImGui() override;
};

} // namespace rb::editor

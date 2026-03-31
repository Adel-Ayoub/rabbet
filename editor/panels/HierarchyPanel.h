#pragma once

#include "editor/Panel.h"

namespace rb::editor {

// Lists the scene's entities and supports create (empty / primitives / lights),
// duplicate, and delete. Selection is written back into the EditorContext.
class HierarchyPanel final : public Panel {
public:
    explicit HierarchyPanel(EditorContext& context) : Panel(context) {}

    [[nodiscard]] const char* name() const override { return "Hierarchy"; }
    void onImGui() override;
};

} // namespace rb::editor

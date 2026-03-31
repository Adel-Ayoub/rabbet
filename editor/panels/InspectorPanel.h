#pragma once

#include "editor/Panel.h"

namespace rb::editor {

// Shows and edits the components of the selected entity. Phase 3 hand-draws the
// built-in components; Phase 4 replaces this with registry-driven generic drawers.
class InspectorPanel final : public Panel {
public:
    explicit InspectorPanel(EditorContext& context) : Panel(context) {}

    [[nodiscard]] const char* name() const override { return "Inspector"; }
    void onImGui() override;
};

} // namespace rb::editor

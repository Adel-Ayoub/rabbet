#pragma once

#include "editor/Panel.h"

namespace rb::editor {

// Shows and edits the components of the selected entity. Every widget is driven by
// the ComponentRegistry: the panel iterates registered types, drawing each via its
// registry draw hook, and the add/remove menu lists every registered component.
class InspectorPanel final : public Panel {
public:
    explicit InspectorPanel(EditorContext& context) : Panel(context) {}

    [[nodiscard]] const char* name() const override { return "Inspector"; }
    void onImGui() override;
};

} // namespace rb::editor

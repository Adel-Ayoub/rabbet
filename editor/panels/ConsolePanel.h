#pragma once

#include "editor/Panel.h"

namespace rb::editor {

class LogBuffer;

// Shows the engine log live, coloured by level, with a minimum-level filter, an
// autoscroll toggle, and a clear button. Reads from the editor's LogBuffer.
class ConsolePanel final : public Panel {
public:
    ConsolePanel(EditorContext& context, LogBuffer& log) : Panel(context), m_log(log) {}

    [[nodiscard]] const char* name() const override { return "Console"; }
    void onImGui() override;

private:
    LogBuffer& m_log;
    int m_minLevel = 0; // index into the Trace..Error filter list
    bool m_autoScroll = true;
};

} // namespace rb::editor

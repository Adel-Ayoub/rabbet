#pragma once

#include "editor/Panel.h"

#include <array>

namespace rb::editor {

class LogBuffer;

// Shows the engine log live, coloured by level, with per-level filter chips (each
// carrying its count), an autoscroll toggle, and a clear button. Reads from the
// editor's LogBuffer.
class ConsolePanel final : public Panel {
public:
    ConsolePanel(EditorContext& context, LogBuffer& log) : Panel(context), m_log(log) {}

    [[nodiscard]] const char* name() const override { return "Console"; }
    void onImGui() override;

private:
    LogBuffer& m_log;
    std::array<bool, 5> m_levelEnabled{true, true, true, true, true}; // Trace..Error
    bool m_autoScroll = true;
};

} // namespace rb::editor

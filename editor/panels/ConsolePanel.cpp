#include "editor/panels/ConsolePanel.h"

#include "editor/Icons.h"
#include "editor/LogBuffer.h"
#include "editor/Palette.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace rb::editor {
namespace {

ImVec4 levelColor(rb::LogLevel level) {
    const Palette& p = palette();
    switch (level) {
        case rb::LogLevel::Trace: return p.inkMuted;
        case rb::LogLevel::Debug: return p.info;
        case rb::LogLevel::Info: return p.ink;
        case rb::LogLevel::Warn: return p.warn;
        case rb::LogLevel::Error: return p.danger;
        case rb::LogLevel::Off: break;
    }
    return p.ink;
}

const char* levelGlyph(rb::LogLevel level) {
    switch (level) {
        case rb::LogLevel::Trace: return icon::kList;
        case rb::LogLevel::Debug: return icon::kBug;
        case rb::LogLevel::Info: return icon::kInfo;
        case rb::LogLevel::Warn: return icon::kTriangleAlert;
        case rb::LogLevel::Error: return icon::kCircleX;
        case rb::LogLevel::Off: break;
    }
    return icon::kInfo;
}

const char* levelName(rb::LogLevel level) {
    switch (level) {
        case rb::LogLevel::Trace: return "Trace";
        case rb::LogLevel::Debug: return "Debug";
        case rb::LogLevel::Info: return "Info";
        case rb::LogLevel::Warn: return "Warn";
        case rb::LogLevel::Error: return "Error";
        case rb::LogLevel::Off: break;
    }
    return "?";
}

const char* levelTag(rb::LogLevel level) {
    switch (level) {
        case rb::LogLevel::Trace: return "TRACE";
        case rb::LogLevel::Debug: return "DEBUG";
        case rb::LogLevel::Info: return "INFO ";
        case rb::LogLevel::Warn: return "WARN ";
        case rb::LogLevel::Error: return "ERROR";
        case rb::LogLevel::Off: break;
    }
    return "?????";
}

} // namespace

void ConsolePanel::onImGui() {
    const Palette& p = palette();
    const std::vector<LogBuffer::Entry> entries = m_log.snapshot();

    std::array<int, 5> counts{};
    for (const LogBuffer::Entry& entry : entries) {
        const auto level = static_cast<std::size_t>(entry.level);
        if (level < counts.size()) {
            ++counts[level];
        }
    }

    // One toggle chip per level, carrying its count.
    for (std::size_t i = 0; i < m_levelEnabled.size(); ++i) {
        const auto level = static_cast<rb::LogLevel>(i);
        char label[48];
        std::snprintf(label, sizeof(label), "%s %d##chip%zu", levelGlyph(level), counts[i], i);
        const bool enabled = m_levelEnabled[i];
        ImGui::PushStyleColor(ImGuiCol_Text, enabled ? levelColor(level) : p.inkMuted);
        ImGui::PushStyleColor(ImGuiCol_Button, enabled ? p.layer : p.well);
        if (ImGui::SmallButton(label)) {
            m_levelEnabled[i] = !enabled;
        }
        ImGui::PopStyleColor(2);
        ImGui::SetItemTooltip("%s", levelName(level));
        ImGui::SameLine(0.0f, 4.0f);
    }

    ImGui::SameLine(0.0f, 12.0f);
    ImGui::Checkbox("Autoscroll", &m_autoScroll);
    ImGui::SameLine();
    if (ImGui::SmallButton(icon::kTrash)) {
        m_log.clear();
    }
    ImGui::SetItemTooltip("Clear the log");
    ImGui::Separator();

    ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImGui::PushFont(fonts().mono);
    std::vector<std::size_t> visible;
    visible.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        const auto level = static_cast<std::size_t>(entries[i].level);
        if (level < m_levelEnabled.size() && m_levelEnabled[level]) {
            visible.push_back(i);
        }
    }
    if (visible.empty()) {
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::TextDisabled("%s", entries.empty() ? "No log output yet"
                                                  : "Nothing matches the level filter");
    } else {
        // Clipped: only the rows in view are submitted. A per-tick warning pins the buffer at
        // its cap, and unclipped text made this the most expensive panel in a busy session.
        ImGuiListClipper clipper;
        clipper.Begin(static_cast<int>(visible.size()));
        while (clipper.Step()) {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
                const LogBuffer::Entry& entry = entries[visible[static_cast<std::size_t>(row)]];
                ImGui::PushStyleColor(ImGuiCol_Text, levelColor(entry.level));
                ImGui::Text("%s  %s", levelTag(entry.level), entry.message.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::PopFont();
    }
    if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

} // namespace rb::editor

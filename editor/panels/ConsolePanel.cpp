#include "editor/panels/ConsolePanel.h"

#include "editor/LogBuffer.h"
#include "editor/Palette.h"

#include <imgui.h>

#include <array>
#include <cstddef>
#include <vector>

namespace rb::editor {
namespace {

constexpr std::array<const char*, 5> kLevelNames{"Trace", "Debug", "Info", "Warn", "Error"};

ImVec4 levelColor(rb::LogLevel level) {
    switch (level) {
        case rb::LogLevel::Trace: return ImVec4(0.55f, 0.57f, 0.60f, 1.0f);
        case rb::LogLevel::Debug: return ImVec4(0.45f, 0.70f, 1.00f, 1.0f);
        case rb::LogLevel::Info: return ImVec4(0.86f, 0.87f, 0.88f, 1.0f);
        case rb::LogLevel::Warn: return ImVec4(0.95f, 0.70f, 0.30f, 1.0f);
        case rb::LogLevel::Error: return ImVec4(0.92f, 0.36f, 0.36f, 1.0f);
        case rb::LogLevel::Off: break;
    }
    return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
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
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("##level", &m_minLevel, kLevelNames.data(), static_cast<int>(kLevelNames.size()));
    ImGui::SameLine();
    ImGui::Checkbox("Autoscroll", &m_autoScroll);
    ImGui::SameLine();
    if (ImGui::Button("Clear")) {
        m_log.clear();
    }
    ImGui::Separator();

    ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_None,
                      ImGuiWindowFlags_HorizontalScrollbar);
    ImFont* mono = monoFont();
    if (mono != nullptr) {
        ImGui::PushFont(mono);
    }
    const std::vector<LogBuffer::Entry> entries = m_log.snapshot();
    std::vector<std::size_t> visible;
    visible.reserve(entries.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (static_cast<int>(entries[i].level) >= m_minLevel) {
            visible.push_back(i);
        }
    }
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
    if (mono != nullptr) {
        ImGui::PopFont();
    }
    if (m_autoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) {
        ImGui::SetScrollHereY(1.0f);
    }
    ImGui::EndChild();
}

} // namespace rb::editor

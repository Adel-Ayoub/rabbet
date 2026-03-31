#include "editor/panels/ViewportPanel.h"

#include "editor/EditorContext.h"

#include <imgui.h>

#include <algorithm>

namespace rb::editor {

void ViewportPanel::draw() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    const bool visible = ImGui::Begin(name(), &openRef());
    ImGui::PopStyleVar();
    if (visible) {
        onImGui();
    }
    ImGui::End();
}

void ViewportPanel::onImGui() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    m_context.viewportWidth = std::max(1, static_cast<int>(avail.x));
    m_context.viewportHeight = std::max(1, static_cast<int>(avail.y));
    m_context.viewportHovered = ImGui::IsWindowHovered();
    m_context.viewportFocused = ImGui::IsWindowFocused();

    if (m_context.viewportTexture != 0 && avail.x > 0.0f && avail.y > 0.0f) {
        // Flip V: GL textures have their origin at the bottom-left.
        const ImTextureID tex = static_cast<ImTextureID>(m_context.viewportTexture);
        ImGui::Image(tex, avail, ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
    }
}

} // namespace rb::editor

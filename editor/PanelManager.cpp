#include "editor/PanelManager.h"

#include <imgui.h>

namespace rb::editor {

void PanelManager::render() {
    for (std::unique_ptr<Panel>& panel : m_panels) {
        if (panel->open()) {
            panel->draw();
        }
    }
}

void PanelManager::renderMenuItems() {
    for (std::unique_ptr<Panel>& panel : m_panels) {
        ImGui::MenuItem(panel->name(), nullptr, &panel->openRef());
    }
}

} // namespace rb::editor

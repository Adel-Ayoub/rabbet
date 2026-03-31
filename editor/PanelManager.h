#pragma once

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "editor/Panel.h"

namespace rb::editor {

// Owns the editor's panels, draws the open ones each frame, and emits a View-menu
// toggle for each.
class PanelManager {
public:
    template <typename T, typename... Args>
    T& add(Args&&... args) {
        static_assert(std::is_base_of_v<Panel, T>, "T must derive from rb::editor::Panel");
        auto panel = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *panel;
        m_panels.push_back(std::move(panel));
        return ref;
    }

    void render();
    void renderMenuItems();

private:
    std::vector<std::unique_ptr<Panel>> m_panels;
};

} // namespace rb::editor

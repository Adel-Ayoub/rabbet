#pragma once

#include <imgui.h>

namespace rb::editor {

struct EditorContext;

// Base class for editor panels. The default draw() wraps the panel content in a
// dockable ImGui window with a close button bound to the open flag; panels that
// need a custom window (e.g. zero-padding) override draw().
class Panel {
public:
    explicit Panel(EditorContext& context) : m_context(context) {}
    virtual ~Panel() = default;
    Panel(const Panel&) = delete;
    Panel& operator=(const Panel&) = delete;

    [[nodiscard]] virtual const char* name() const = 0;
    virtual void onImGui() = 0;

    virtual void draw() {
        const bool visible = ImGui::Begin(name(), &m_open);
        if (visible) {
            onImGui();
        }
        ImGui::End();
    }

    [[nodiscard]] bool open() const noexcept { return m_open; }
    [[nodiscard]] bool& openRef() noexcept { return m_open; }

protected:
    EditorContext& m_context;
    bool m_open = true;
};

} // namespace rb::editor

#pragma once

#include <optional>
#include <string>

#include "rabbet/core/Runtime.h"
#include "rabbet/render/gl/Framebuffer.h"
#include "rabbet/serialize/ComponentRegistry.h"

#include "editor/EditorCamera.h"
#include "editor/EditorContext.h"
#include "editor/LogBuffer.h"
#include "editor/PanelManager.h"

namespace rb {
class Window;
class RenderDevice;
} // namespace rb

namespace rb::editor {

// The editor shell: owns the engine runtime, the dockable ImGui UI, the offscreen
// viewport framebuffer, and the free-look camera, and drives the per-frame loop
// that renders the scene to a texture shown in the Viewport panel.
class Editor {
public:
    Editor(rb::Window& window, rb::RenderDevice& device);
    ~Editor();
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;
    Editor(Editor&&) = delete;
    Editor& operator=(Editor&&) = delete;

    void run();

private:
    void buildDefaultScene();
    void drawDockspaceAndMenu();
    void buildDefaultLayout(unsigned int dockId);
    void renderScene(int width, int height, float dt);
    void newScene();
    void openScene();
    void saveScene();

    rb::Window& m_window;
    rb::RenderDevice& m_device;
    rb::Runtime m_runtime;
    rb::ComponentRegistry m_registry;
    LogBuffer m_log;
    EditorContext m_context;
    PanelManager m_panels;
    EditorCamera m_camera;
    std::optional<rb::gl::Framebuffer> m_framebuffer;
    std::string m_scenePath = "rabbet_editor.scene.json";
    bool m_cameraActive = false;
};

} // namespace rb::editor

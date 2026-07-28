#pragma once

#include <optional>

#include <nlohmann/json.hpp>

#include "rabbet/core/Runtime.h"
#include "rabbet/render/PostProcessor.h"
#include "rabbet/render/gl/Framebuffer.h"
#include "rabbet/serialize/ComponentRegistry.h"

#include "editor/EditorCamera.h"
#include "editor/EditorContext.h"
#include "editor/LogBuffer.h"
#include "editor/PanelManager.h"
#include "editor/ThumbnailRenderer.h"
#include "editor/Toasts.h"

namespace rb {
class Window;
class RenderDevice;
class RenderSystem;
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
    void drawTransport();
    void drawStatusBar();
    void buildDefaultLayout(unsigned int dockId);
    void renderScene(int width, int height, float dt);
    void newScene();
    void openScene();
    void saveScene();
    void saveSceneAs();
    void startPlay();
    void stopPlay();

    rb::Window& m_window;
    rb::RenderDevice& m_device;
    rb::Runtime m_runtime;
    rb::ComponentRegistry m_registry;
    LogBuffer m_log;
    EditorContext m_context;
    PanelManager m_panels;
    EditorCamera m_camera;
    std::optional<rb::gl::Framebuffer> m_framebuffer;
    bool m_framebufferHdr = false; // the scene FBO is RGBA16F while a post-process is active
    rb::PostProcessor m_postProcessor;          // HDR -> LDR chain, run when post-processing is on
    ThumbnailRenderer m_thumbnails;             // offscreen asset previews for the Assets panel
    Toasts m_toasts;                            // transient overlay notifications
    rb::RenderSystem* m_renderSystem = nullptr; // owned by m_runtime; used for picking
    bool m_cameraActive = false;
    bool m_nfdReady = false; // NFD_Init succeeded; the dialog actions no-op with an error when false

    // Play mode: a snapshot is taken on Play and reloaded on Stop, so gameplay edits
    // never touch the authored scene. Pause freezes Play systems; Step runs one frame.
    nlohmann::json m_snapshot;
    bool m_playSession = false;
    bool m_paused = false;
    bool m_step = false;
};

} // namespace rb::editor

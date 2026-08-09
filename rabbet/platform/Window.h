#pragma once

#include <memory>
#include <optional>
#include <string>

struct GLFWwindow;

namespace rb {

namespace gl {
class Device;
}

enum class WindowClientApi { OpenGL, None };

struct WindowConfig {
    int width = 1280;
    int height = 720;
    std::string title = "Rabbet";
    bool vsync = true;
    bool resizable = true;
    bool fullscreen = false; // borderless, sized to the primary monitor
    WindowClientApi clientApi = WindowClientApi::OpenGL;
};

class Window {
public:
    [[nodiscard]] static std::optional<Window> create(const WindowConfig& config);

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    ~Window();

    [[nodiscard]] bool shouldClose() const noexcept;
    void requestClose() const noexcept;
    void pollEvents() const noexcept;
    void setCursorCaptured(bool captured) const noexcept;

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] WindowClientApi clientApi() const noexcept { return m_clientApi; }
    [[nodiscard]] GLFWwindow* handle() const noexcept { return m_handle.get(); }

private:
    friend class gl::Device;

    Window(std::shared_ptr<GLFWwindow> handle, WindowClientApi clientApi) noexcept;
    void destroy() noexcept;

    std::shared_ptr<GLFWwindow> m_handle;
    WindowClientApi m_clientApi = WindowClientApi::OpenGL;
};

} // namespace rb

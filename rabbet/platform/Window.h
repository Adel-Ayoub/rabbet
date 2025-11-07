#pragma once

#include <optional>
#include <string>

struct GLFWwindow;

namespace rb {

struct WindowConfig {
    int width = 1280;
    int height = 720;
    std::string title = "Rabbet";
    bool vsync = true;
    bool resizable = true;
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
    void swapBuffers() const noexcept;
    void pollEvents() const noexcept;
    void setCursorCaptured(bool captured) const noexcept;

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;
    [[nodiscard]] GLFWwindow* handle() const noexcept { return m_handle; }

private:
    explicit Window(GLFWwindow* handle) noexcept;
    void destroy() noexcept;

    GLFWwindow* m_handle = nullptr;
};

} // namespace rb

#include "rabbet/platform/Window.h"

#include "rabbet/util/Log.h"

#include <glad/glad.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <utility>

namespace rb {

namespace {

int g_liveWindows = 0;

void onGlfwError(int code, const char* description) {
    log::error("GLFW error {}: {}", code, description);
}

void onFramebufferResize(GLFWwindow*, int width, int height) {
    glViewport(0, 0, width, height);
}

} // namespace

std::optional<Window> Window::create(const WindowConfig& config) {
    if (g_liveWindows == 0) {
        glfwSetErrorCallback(onGlfwError);
        if (glfwInit() != GLFW_TRUE) {
            log::error("failed to initialize GLFW");
            return std::nullopt;
        }
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    GLFWwindow* handle =
        glfwCreateWindow(config.width, config.height, config.title.c_str(), nullptr, nullptr);
    if (handle == nullptr) {
        log::error("failed to create window");
        if (g_liveWindows == 0) {
            glfwTerminate();
        }
        return std::nullopt;
    }

    glfwMakeContextCurrent(handle);

    if (gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) == 0) {
        log::error("failed to load OpenGL function pointers");
        glfwDestroyWindow(handle);
        if (g_liveWindows == 0) {
            glfwTerminate();
        }
        return std::nullopt;
    }

    glfwSwapInterval(config.vsync ? 1 : 0);
    glfwSetFramebufferSizeCallback(handle, onFramebufferResize);

    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(handle, &fbWidth, &fbHeight);
    glViewport(0, 0, fbWidth, fbHeight);

    ++g_liveWindows;
    log::info("window created ({}x{}), OpenGL {}", fbWidth, fbHeight,
              reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    return Window(handle);
}

Window::Window(GLFWwindow* handle) noexcept : m_handle(handle) {}

Window::Window(Window&& other) noexcept : m_handle(std::exchange(other.m_handle, nullptr)) {}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
}

Window::~Window() {
    destroy();
}

void Window::destroy() noexcept {
    if (m_handle == nullptr) {
        return;
    }
    glfwDestroyWindow(m_handle);
    m_handle = nullptr;
    if (--g_liveWindows == 0) {
        glfwTerminate();
    }
}

bool Window::shouldClose() const noexcept {
    return glfwWindowShouldClose(m_handle) != 0;
}

void Window::requestClose() const noexcept {
    glfwSetWindowShouldClose(m_handle, GLFW_TRUE);
}

void Window::swapBuffers() const noexcept {
    glfwSwapBuffers(m_handle);
}

void Window::pollEvents() const noexcept {
    glfwPollEvents();
}

int Window::width() const noexcept {
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(m_handle, &w, &h);
    return w;
}

int Window::height() const noexcept {
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(m_handle, &w, &h);
    return h;
}

} // namespace rb

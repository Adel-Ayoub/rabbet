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

void onFramebufferResize(GLFWwindow* window, int width, int height) {
    GLFWwindow* previous = glfwGetCurrentContext();
    if (previous != window) {
        glfwMakeContextCurrent(window);
    }
    glViewport(0, 0, width, height);
    if (previous != window) {
        glfwMakeContextCurrent(previous);
    }
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

    glfwDefaultWindowHints();
    if (config.clientApi == WindowClientApi::OpenGL) {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
    } else {
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    }
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    int width = config.width;
    int height = config.height;
    if (config.fullscreen) {
        if (GLFWmonitor* monitor = glfwGetPrimaryMonitor()) {
            if (const GLFWvidmode* mode = glfwGetVideoMode(monitor)) {
                width = mode->width;
                height = mode->height;
                glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
            }
        }
    }

    GLFWwindow* handle =
        glfwCreateWindow(width, height, config.title.c_str(), nullptr, nullptr);
    if (handle == nullptr) {
        log::error("failed to create window");
        if (g_liveWindows == 0) {
            glfwTerminate();
        }
        return std::nullopt;
    }

    if (config.clientApi == WindowClientApi::OpenGL) {
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
    }

    if (config.fullscreen) {
        glfwSetWindowPos(handle, 0, 0);
    }

    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(handle, &fbWidth, &fbHeight);
    if (config.clientApi == WindowClientApi::OpenGL) {
        glViewport(0, 0, fbWidth, fbHeight);
    }

    ++g_liveWindows;
    std::shared_ptr<GLFWwindow> ownedHandle(handle, [](GLFWwindow* window) {
        glfwDestroyWindow(window);
        if (--g_liveWindows == 0) {
            glfwTerminate();
        }
    });
    if (config.clientApi == WindowClientApi::OpenGL) {
        log::info("window created ({}x{}), OpenGL {}", fbWidth, fbHeight,
                  reinterpret_cast<const char*>(glGetString(GL_VERSION)));
    } else {
        log::info("window created ({}x{}), no client API", fbWidth, fbHeight);
    }
    return Window(std::move(ownedHandle), config.clientApi);
}

Window::Window(std::shared_ptr<GLFWwindow> handle, WindowClientApi clientApi) noexcept
    : m_handle(std::move(handle)), m_clientApi(clientApi) {}

Window::Window(Window&& other) noexcept
    : m_handle(std::move(other.m_handle)), m_clientApi(other.m_clientApi) {}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        destroy();
        m_handle = std::move(other.m_handle);
        m_clientApi = other.m_clientApi;
    }
    return *this;
}

Window::~Window() {
    destroy();
}

void Window::destroy() noexcept {
    m_handle.reset();
}

bool Window::shouldClose() const noexcept {
    return glfwWindowShouldClose(m_handle.get()) != 0;
}

void Window::requestClose() const noexcept {
    glfwSetWindowShouldClose(m_handle.get(), GLFW_TRUE);
}

void Window::pollEvents() const noexcept {
    glfwPollEvents();
}

void Window::setCursorCaptured(bool captured) const noexcept {
    glfwSetInputMode(m_handle.get(), GLFW_CURSOR,
                     captured ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

int Window::width() const noexcept {
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(m_handle.get(), &w, &h);
    return w;
}

int Window::height() const noexcept {
    int w = 0;
    int h = 0;
    glfwGetFramebufferSize(m_handle.get(), &w, &h);
    return h;
}

} // namespace rb

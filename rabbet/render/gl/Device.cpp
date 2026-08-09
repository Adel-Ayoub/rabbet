#include "rabbet/render/gl/Device.h"

#include "rabbet/platform/Window.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace rb::gl {

std::unique_ptr<Device> Device::create(const Window& window) {
    if (window.handle() == nullptr || window.clientApi() != WindowClientApi::OpenGL) {
        log::error("OpenGL device creation needs an OpenGL window");
        return nullptr;
    }
    return std::unique_ptr<Device>(new Device(window.m_handle));
}

Device::Device(std::shared_ptr<GLFWwindow> window) noexcept : m_window(std::move(window)) {}

void Device::makeCurrent() const noexcept {
    if (glfwGetCurrentContext() != m_window.get()) {
        glfwMakeContextCurrent(m_window.get());
    }
}

void Device::setViewport(int width, int height) {
    makeCurrent();
    glViewport(0, 0, width, height);
}

void Device::setClearColor(const glm::vec4& color) {
    makeCurrent();
    glClearColor(color.r, color.g, color.b, color.a);
}

void Device::clear() {
    makeCurrent();
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Device::setDepthTest(bool enabled) {
    makeCurrent();
    if (enabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void Device::setCullFace(bool enabled) {
    makeCurrent();
    if (enabled) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

void Device::present() {
    makeCurrent();
    glfwSwapBuffers(m_window.get());
}

std::string_view Device::backendName() const {
    return "OpenGL";
}

} // namespace rb::gl

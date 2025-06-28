#include "rabbet/render/gl/Device.h"

#include <glad/glad.h>

namespace rb::gl {

void Device::setViewport(int width, int height) {
    glViewport(0, 0, width, height);
}

void Device::setClearColor(const glm::vec4& color) {
    glClearColor(color.r, color.g, color.b, color.a);
}

void Device::clear() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Device::setDepthTest(bool enabled) {
    if (enabled) {
        glEnable(GL_DEPTH_TEST);
    } else {
        glDisable(GL_DEPTH_TEST);
    }
}

void Device::setCullFace(bool enabled) {
    if (enabled) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }
}

std::string_view Device::backendName() const {
    return "OpenGL";
}

} // namespace rb::gl

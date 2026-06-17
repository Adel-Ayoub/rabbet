#include "rabbet/render/gl/ColorTarget.h"

#include "rabbet/util/Log.h"

#include <glad/glad.h>

#include <algorithm>
#include <utility>

namespace rb::gl {

ColorTarget ColorTarget::create(int width, int height, bool hdr) {
    ColorTarget target;
    target.m_hdr = hdr;
    glGenFramebuffers(1, &target.m_fbo);
    glGenTextures(1, &target.m_color);

    glBindTexture(GL_TEXTURE_2D, target.m_color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, target.m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.m_color, 0);
    target.allocate(width, height);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::error("colour target is incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    return target;
}

void ColorTarget::allocate(int width, int height) noexcept {
    m_width = std::max(width, 1);
    m_height = std::max(height, 1);
    glBindTexture(GL_TEXTURE_2D, m_color);
    if (m_hdr) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA, GL_FLOAT, nullptr);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     nullptr);
    }
}

void ColorTarget::bind() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void ColorTarget::resize(int width, int height) {
    const int w = std::max(width, 1);
    const int h = std::max(height, 1);
    if (w == m_width && h == m_height) {
        return;
    }
    allocate(w, h);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ColorTarget::bindTexture(unsigned int unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_color);
}

ColorTarget::ColorTarget(ColorTarget&& other) noexcept
    : m_fbo(std::exchange(other.m_fbo, 0)), m_color(std::exchange(other.m_color, 0)),
      m_width(std::exchange(other.m_width, 0)), m_height(std::exchange(other.m_height, 0)),
      m_hdr(other.m_hdr) {}

ColorTarget& ColorTarget::operator=(ColorTarget&& other) noexcept {
    if (this != &other) {
        destroy();
        m_fbo = std::exchange(other.m_fbo, 0);
        m_color = std::exchange(other.m_color, 0);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
        m_hdr = other.m_hdr;
    }
    return *this;
}

ColorTarget::~ColorTarget() {
    destroy();
}

void ColorTarget::destroy() noexcept {
    if (m_color != 0) {
        glDeleteTextures(1, &m_color);
    }
    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
    }
    m_fbo = 0;
    m_color = 0;
    m_width = 0;
    m_height = 0;
}

} // namespace rb::gl

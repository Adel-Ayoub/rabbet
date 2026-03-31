#include "rabbet/render/gl/Framebuffer.h"

#include "rabbet/util/Log.h"

#include <glad/glad.h>

#include <algorithm>
#include <utility>

namespace rb::gl {

Framebuffer Framebuffer::create(int width, int height) {
    unsigned int fbo = 0;
    unsigned int color = 0;
    unsigned int depth = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &color);
    glGenRenderbuffers(1, &depth);

    glBindTexture(GL_TEXTURE_2D, color);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, color, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);

    Framebuffer framebuffer(fbo, color, depth, 0, 0);
    framebuffer.allocate(width, height);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::error("colour framebuffer is incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    return framebuffer;
}

void Framebuffer::allocate(int width, int height) noexcept {
    m_width = std::max(width, 1);
    m_height = std::max(height, 1);
    glBindTexture(GL_TEXTURE_2D, m_color);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 nullptr);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_width, m_height);
}

Framebuffer::Framebuffer(Framebuffer&& other) noexcept
    : m_fbo(std::exchange(other.m_fbo, 0)), m_color(std::exchange(other.m_color, 0)),
      m_depth(std::exchange(other.m_depth, 0)), m_width(std::exchange(other.m_width, 0)),
      m_height(std::exchange(other.m_height, 0)) {}

Framebuffer& Framebuffer::operator=(Framebuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        m_fbo = std::exchange(other.m_fbo, 0);
        m_color = std::exchange(other.m_color, 0);
        m_depth = std::exchange(other.m_depth, 0);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
    }
    return *this;
}

Framebuffer::~Framebuffer() {
    destroy();
}

void Framebuffer::destroy() noexcept {
    if (m_color != 0) {
        glDeleteTextures(1, &m_color);
    }
    if (m_depth != 0) {
        glDeleteRenderbuffers(1, &m_depth);
    }
    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
    }
    m_color = 0;
    m_depth = 0;
    m_fbo = 0;
}

void Framebuffer::bind() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void Framebuffer::unbind() noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void Framebuffer::resize(int width, int height) {
    const int w = std::max(width, 1);
    const int h = std::max(height, 1);
    if (w == m_width && h == m_height) {
        return;
    }
    allocate(w, h);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
}

} // namespace rb::gl

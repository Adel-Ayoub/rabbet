#include "rabbet/render/gl/DepthMap.h"

#include "rabbet/util/Log.h"

#include <glad/glad.h>

#include <array>
#include <utility>

namespace rb::gl {

DepthMap DepthMap::create(int width, int height) {
    unsigned int fbo = 0;
    unsigned int texture = 0;
    glGenFramebuffers(1, &fbo);
    glGenTextures(1, &texture);

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, width, height, 0, GL_DEPTH_COMPONENT,
                 GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const std::array<float, 4> border{1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border.data());

    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texture, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        log::error("depth framebuffer is incomplete");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    return DepthMap(fbo, texture, width, height);
}

DepthMap::DepthMap(DepthMap&& other) noexcept
    : m_fbo(std::exchange(other.m_fbo, 0)), m_texture(std::exchange(other.m_texture, 0)),
      m_width(std::exchange(other.m_width, 0)), m_height(std::exchange(other.m_height, 0)) {}

DepthMap& DepthMap::operator=(DepthMap&& other) noexcept {
    if (this != &other) {
        destroy();
        m_fbo = std::exchange(other.m_fbo, 0);
        m_texture = std::exchange(other.m_texture, 0);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
    }
    return *this;
}

DepthMap::~DepthMap() {
    destroy();
}

void DepthMap::destroy() noexcept {
    if (m_texture != 0) {
        glDeleteTextures(1, &m_texture);
    }
    if (m_fbo != 0) {
        glDeleteFramebuffers(1, &m_fbo);
    }
    m_texture = 0;
    m_fbo = 0;
}

void DepthMap::bindForWriting() const noexcept {
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_width, m_height);
}

void DepthMap::bindTexture(unsigned int unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_texture);
}

} // namespace rb::gl

#include "rabbet/render/gl/UniformBuffer.h"

#include <glad/glad.h>

#include <utility>

namespace rb::gl {

UniformBuffer UniformBuffer::create(std::size_t sizeBytes) {
    unsigned int buffer = 0;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, buffer);
    glBufferData(GL_UNIFORM_BUFFER, static_cast<GLsizeiptr>(sizeBytes), nullptr, GL_DYNAMIC_DRAW);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
    return UniformBuffer(buffer);
}

UniformBuffer::UniformBuffer(UniformBuffer&& other) noexcept
    : m_buffer(std::exchange(other.m_buffer, 0)) {}

UniformBuffer& UniformBuffer::operator=(UniformBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        m_buffer = std::exchange(other.m_buffer, 0);
    }
    return *this;
}

UniformBuffer::~UniformBuffer() {
    destroy();
}

void UniformBuffer::destroy() noexcept {
    if (m_buffer != 0) {
        glDeleteBuffers(1, &m_buffer);
    }
    m_buffer = 0;
}

void UniformBuffer::upload(const void* data, std::size_t sizeBytes) const noexcept {
    glBindBuffer(GL_UNIFORM_BUFFER, m_buffer);
    glBufferSubData(GL_UNIFORM_BUFFER, 0, static_cast<GLsizeiptr>(sizeBytes), data);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void UniformBuffer::bindBase(unsigned int index) const noexcept {
    // glBindBufferBase also rebinds the generic target, so clear it to keep the
    // class leaving only the indexed binding behind.
    glBindBufferBase(GL_UNIFORM_BUFFER, index, m_buffer);
    glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

} // namespace rb::gl

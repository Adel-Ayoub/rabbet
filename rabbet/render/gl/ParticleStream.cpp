#include "rabbet/render/gl/ParticleStream.h"

#include <glad/glad.h>

#include <cstddef>
#include <utility>

namespace rb::gl {

ParticleStream ParticleStream::create() {
    ParticleStream stream;
    glGenVertexArrays(1, &stream.m_vao);
    glGenBuffers(1, &stream.m_vbo);

    glBindVertexArray(stream.m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, stream.m_vbo);

    const auto stride = static_cast<GLsizei>(sizeof(ParticleVertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(ParticleVertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(ParticleVertex, uv)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(ParticleVertex, color)));

    glBindVertexArray(0);
    return stream;
}

void ParticleStream::upload(std::span<const ParticleVertex> vertices) {
    m_vertexCount = vertices.size();
    const auto bytes = static_cast<GLsizeiptr>(vertices.size_bytes());

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    if (vertices.size_bytes() > m_capacityBytes) {
        glBufferData(GL_ARRAY_BUFFER, bytes, vertices.data(), GL_DYNAMIC_DRAW);
        m_capacityBytes = vertices.size_bytes();
    } else {
        // Orphan the current store (let the driver hand back fresh memory, no sync stall), then
        // overwrite the prefix we actually use.
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_capacityBytes), nullptr,
                     GL_DYNAMIC_DRAW);
        if (bytes > 0) {
            glBufferSubData(GL_ARRAY_BUFFER, 0, bytes, vertices.data());
        }
    }
    glBindVertexArray(0);
}

void ParticleStream::draw() const noexcept {
    if (m_vertexCount == 0) {
        return;
    }
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertexCount));
    glBindVertexArray(0);
}

ParticleStream::ParticleStream(ParticleStream&& other) noexcept
    : m_vao(std::exchange(other.m_vao, 0)), m_vbo(std::exchange(other.m_vbo, 0)),
      m_vertexCount(std::exchange(other.m_vertexCount, 0)),
      m_capacityBytes(std::exchange(other.m_capacityBytes, 0)) {}

ParticleStream& ParticleStream::operator=(ParticleStream&& other) noexcept {
    if (this != &other) {
        destroy();
        m_vao = std::exchange(other.m_vao, 0);
        m_vbo = std::exchange(other.m_vbo, 0);
        m_vertexCount = std::exchange(other.m_vertexCount, 0);
        m_capacityBytes = std::exchange(other.m_capacityBytes, 0);
    }
    return *this;
}

ParticleStream::~ParticleStream() {
    destroy();
}

void ParticleStream::destroy() noexcept {
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
    }
    m_vao = 0;
    m_vbo = 0;
    m_vertexCount = 0;
    m_capacityBytes = 0;
}

} // namespace rb::gl

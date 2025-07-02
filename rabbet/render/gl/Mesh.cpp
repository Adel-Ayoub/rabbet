#include "rabbet/render/gl/Mesh.h"

#include <glad/glad.h>

#include <utility>

namespace rb::gl {

Mesh Mesh::create(std::span<const Vertex> vertices, std::span<const std::uint32_t> indices) {
    Mesh mesh;
    mesh.m_indexCount = indices.size();

    glGenVertexArrays(1, &mesh.m_vao);
    glGenBuffers(1, &mesh.m_vbo);
    glGenBuffers(1, &mesh.m_ebo);

    glBindVertexArray(mesh.m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, mesh.m_vbo);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size_bytes()), vertices.data(),
                 GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLsizeiptr>(indices.size_bytes()),
                 indices.data(), GL_STATIC_DRAW);

    const auto stride = static_cast<GLsizei>(sizeof(Vertex));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
                          reinterpret_cast<const void*>(offsetof(Vertex, uv)));

    glBindVertexArray(0);
    return mesh;
}

Mesh Mesh::create(const MeshData& data) {
    return create(data.vertices, data.indices);
}

Mesh::Mesh(Mesh&& other) noexcept
    : m_vao(std::exchange(other.m_vao, 0)), m_vbo(std::exchange(other.m_vbo, 0)),
      m_ebo(std::exchange(other.m_ebo, 0)), m_indexCount(std::exchange(other.m_indexCount, 0)) {}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        destroy();
        m_vao = std::exchange(other.m_vao, 0);
        m_vbo = std::exchange(other.m_vbo, 0);
        m_ebo = std::exchange(other.m_ebo, 0);
        m_indexCount = std::exchange(other.m_indexCount, 0);
    }
    return *this;
}

Mesh::~Mesh() {
    destroy();
}

void Mesh::destroy() noexcept {
    if (m_ebo != 0) {
        glDeleteBuffers(1, &m_ebo);
    }
    if (m_vbo != 0) {
        glDeleteBuffers(1, &m_vbo);
    }
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
    }
    m_vao = 0;
    m_vbo = 0;
    m_ebo = 0;
    m_indexCount = 0;
}

void Mesh::draw() const noexcept {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(m_indexCount), GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

} // namespace rb::gl

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "rabbet/render/MeshData.h"
#include "rabbet/render/Vertex.h"

namespace rb::gl {

class Mesh {
public:
    [[nodiscard]] static Mesh create(std::span<const Vertex> vertices,
                                     std::span<const std::uint32_t> indices);
    [[nodiscard]] static Mesh create(const MeshData& data);

    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    ~Mesh();

    void draw() const noexcept;

    [[nodiscard]] std::size_t indexCount() const noexcept { return m_indexCount; }

private:
    Mesh() = default;
    void destroy() noexcept;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    unsigned int m_ebo = 0;
    std::size_t m_indexCount = 0;
};

} // namespace rb::gl

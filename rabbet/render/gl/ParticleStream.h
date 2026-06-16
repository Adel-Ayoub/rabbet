#pragma once

#include <cstddef>
#include <span>

#include <glm/glm.hpp>

namespace rb::gl {

// One billboard corner the CPU streams to the GPU: a world-space position (the camera-facing quad
// is expanded on the CPU from the view basis), a sprite uv, and a premultiplied-by-life rgba tint.
struct ParticleVertex {
    glm::vec3 position;
    glm::vec2 uv;
    glm::vec4 color;
};

// A dynamic billboard vertex stream: a VAO + a single VBO refilled each frame and drawn as
// non-indexed triangles. The particle pass expands each live particle into two triangles on the
// CPU and streams them here. RAII, move-only, mirroring gl::Mesh.
class ParticleStream {
public:
    [[nodiscard]] static ParticleStream create();

    ParticleStream(const ParticleStream&) = delete;
    ParticleStream& operator=(const ParticleStream&) = delete;
    ParticleStream(ParticleStream&& other) noexcept;
    ParticleStream& operator=(ParticleStream&& other) noexcept;
    ~ParticleStream();

    // Replaces the buffer contents and records the vertex count for draw(). Grows the GPU storage
    // (orphaning the old) only when the data exceeds the current capacity; otherwise orphans and
    // sub-updates in place — the standard stall-free streaming path for per-frame dynamic data.
    void upload(std::span<const ParticleVertex> vertices);
    void draw() const noexcept;

    [[nodiscard]] std::size_t vertexCount() const noexcept { return m_vertexCount; }

private:
    ParticleStream() = default;
    void destroy() noexcept;

    unsigned int m_vao = 0;
    unsigned int m_vbo = 0;
    std::size_t m_vertexCount = 0;
    std::size_t m_capacityBytes = 0;
};

} // namespace rb::gl

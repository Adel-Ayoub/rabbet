#pragma once

#include <cstddef>

namespace rb::gl {

// A uniform block backing store. The camera block of the dialect shaders is its first
// user; a pass uploads the block bytes and binds the buffer to the binding index the
// program was wired to. RAII, move-only, mirroring Framebuffer.
class UniformBuffer {
public:
    [[nodiscard]] static UniformBuffer create(std::size_t sizeBytes);

    UniformBuffer(const UniformBuffer&) = delete;
    UniformBuffer& operator=(const UniformBuffer&) = delete;
    UniformBuffer(UniformBuffer&& other) noexcept;
    UniformBuffer& operator=(UniformBuffer&& other) noexcept;
    ~UniformBuffer();

    // sizeBytes must be at most the creation size.
    void upload(const void* data, std::size_t sizeBytes) const noexcept;
    void bindBase(unsigned int index) const noexcept;

    [[nodiscard]] unsigned int id() const noexcept { return m_buffer; }
    [[nodiscard]] bool valid() const noexcept { return m_buffer != 0; }

private:
    explicit UniformBuffer(unsigned int buffer) noexcept : m_buffer(buffer) {}
    void destroy() noexcept;

    unsigned int m_buffer = 0;
};

} // namespace rb::gl

#pragma once

#include <cstdint>

namespace rb::gl {

// An offscreen integer render target for mouse picking: one R32I colour texture
// storing a per-pixel entity index (cleared to -1 = "nothing"), plus a depth buffer.
// RAII, move-only. Render the scene with a flat id shader, then readPixel() the click.
class PickBuffer {
public:
    static constexpr std::int32_t kNone = -1;

    [[nodiscard]] static PickBuffer create(int width, int height);

    PickBuffer(const PickBuffer&) = delete;
    PickBuffer& operator=(const PickBuffer&) = delete;
    PickBuffer(PickBuffer&& other) noexcept;
    PickBuffer& operator=(PickBuffer&& other) noexcept;
    ~PickBuffer();

    void resize(int width, int height);
    // Binds the buffer, sets the viewport, and clears colour to kNone and depth to 1.
    void bindAndClear() noexcept;
    static void unbind() noexcept;
    // Reads the stored index at (x, y), where y is measured from the bottom (GL origin).
    [[nodiscard]] std::int32_t readPixel(int x, int y) const noexcept;

    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }

private:
    PickBuffer(unsigned int fbo, unsigned int color, unsigned int depth, int width,
               int height) noexcept
        : m_fbo(fbo), m_color(color), m_depth(depth), m_width(width), m_height(height) {}
    void destroy() noexcept;
    void allocate(int width, int height) noexcept;

    unsigned int m_fbo = 0;
    unsigned int m_color = 0;
    unsigned int m_depth = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace rb::gl

#pragma once

namespace rb::gl {

// A colour-only offscreen target (one texture + FBO, no depth) for full-screen post passes: bloom
// mips, ping-pong blur, the composite + FXAA outputs. RGBA16F when `hdr` (bloom / HDR work), else
// RGBA8 (final LDR). Bilinear + clamp so up/downsample taps filter and the edges don't wrap. RAII,
// move-only, mirroring gl::Framebuffer.
class ColorTarget {
public:
    [[nodiscard]] static ColorTarget create(int width, int height, bool hdr);

    ColorTarget(const ColorTarget&) = delete;
    ColorTarget& operator=(const ColorTarget&) = delete;
    ColorTarget(ColorTarget&& other) noexcept;
    ColorTarget& operator=(ColorTarget&& other) noexcept;
    ~ColorTarget();

    void bind() const noexcept; // bind the FBO and set the viewport to this target's size
    void resize(int width, int height);
    void bindTexture(unsigned int unit) const noexcept;

    [[nodiscard]] unsigned int texture() const noexcept { return m_color; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }

private:
    ColorTarget() = default;
    void allocate(int width, int height) noexcept;
    void destroy() noexcept;

    unsigned int m_fbo = 0;
    unsigned int m_color = 0;
    int m_width = 0;
    int m_height = 0;
    bool m_hdr = false;
};

} // namespace rb::gl

#pragma once

namespace rb::gl {

// An offscreen render target: one RGBA8 colour texture (sampled by the editor
// viewport) plus a depth renderbuffer (written, never sampled). RAII, move-only,
// mirroring DepthMap. resize() reuses the same GL object ids so a texture handle
// handed to ImGui stays valid across viewport resizes.
class Framebuffer {
public:
    [[nodiscard]] static Framebuffer create(int width, int height);

    Framebuffer(const Framebuffer&) = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;
    Framebuffer(Framebuffer&& other) noexcept;
    Framebuffer& operator=(Framebuffer&& other) noexcept;
    ~Framebuffer();

    void bind() const noexcept;
    static void unbind() noexcept;
    void resize(int width, int height);

    [[nodiscard]] unsigned int colorTexture() const noexcept { return m_color; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] bool valid() const noexcept { return m_fbo != 0; }

private:
    Framebuffer(unsigned int fbo, unsigned int color, unsigned int depth, int width,
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

#pragma once

namespace rb::gl {

class DepthMap {
public:
    [[nodiscard]] static DepthMap create(int width, int height);

    DepthMap(const DepthMap&) = delete;
    DepthMap& operator=(const DepthMap&) = delete;
    DepthMap(DepthMap&& other) noexcept;
    DepthMap& operator=(DepthMap&& other) noexcept;
    ~DepthMap();

    void bindForWriting() const noexcept;
    void bindTexture(unsigned int unit) const noexcept;

    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }

private:
    DepthMap(unsigned int fbo, unsigned int texture, int width, int height) noexcept
        : m_fbo(fbo), m_texture(texture), m_width(width), m_height(height) {}
    void destroy() noexcept;

    unsigned int m_fbo = 0;
    unsigned int m_texture = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace rb::gl

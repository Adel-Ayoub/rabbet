#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace rb::gl {

struct TextureConfig {
    bool srgb = false;
    bool generateMipmaps = true;
    bool linearFilter = true;
    bool repeat = true;
};

class Texture {
public:
    [[nodiscard]] static Texture fromPixels(std::span<const std::byte> pixels, int width, int height,
                                            int channels, const TextureConfig& config = {});
    [[nodiscard]] static Texture solid(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                                       std::uint8_t a = 255);

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;
    ~Texture();

    void bind(unsigned int unit = 0) const noexcept;

    [[nodiscard]] unsigned int id() const noexcept { return m_id; }
    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }

private:
    Texture(unsigned int id, int width, int height) noexcept
        : m_id(id), m_width(width), m_height(height) {}
    void destroy() noexcept;

    unsigned int m_id = 0;
    int m_width = 0;
    int m_height = 0;
};

} // namespace rb::gl

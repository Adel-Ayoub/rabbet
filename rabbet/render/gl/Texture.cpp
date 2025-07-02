#include "rabbet/render/gl/Texture.h"

#include <glad/glad.h>

#include <array>
#include <utility>

namespace rb::gl {
namespace {

void resolveFormats(int channels, bool srgb, GLint& internalFormat, GLenum& format) {
    switch (channels) {
        case 1:
            internalFormat = GL_R8;
            format = GL_RED;
            break;
        case 3:
            internalFormat = srgb ? GL_SRGB8 : GL_RGB8;
            format = GL_RGB;
            break;
        case 4:
        default:
            internalFormat = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
            format = GL_RGBA;
            break;
    }
}

} // namespace

Texture Texture::fromPixels(std::span<const std::byte> pixels, int width, int height, int channels,
                            const TextureConfig& config) {
    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);

    const GLint wrap = config.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    const GLint minFilter =
        config.linearFilter ? (config.generateMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR)
                            : GL_NEAREST;
    const GLint magFilter = config.linearFilter ? GL_LINEAR : GL_NEAREST;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);

    GLint internalFormat = GL_RGBA8;
    GLenum format = GL_RGBA;
    resolveFormats(channels, config.srgb, internalFormat, format);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE,
                 pixels.data());
    if (config.generateMipmaps) {
        glGenerateMipmap(GL_TEXTURE_2D);
    }
    glBindTexture(GL_TEXTURE_2D, 0);
    return Texture(id, width, height);
}

Texture Texture::solid(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a) {
    const std::array<std::byte, 4> pixel{std::byte{r}, std::byte{g}, std::byte{b}, std::byte{a}};
    TextureConfig config;
    config.generateMipmaps = false;
    config.linearFilter = false;
    return fromPixels(pixel, 1, 1, 4, config);
}

Texture::Texture(Texture&& other) noexcept
    : m_id(std::exchange(other.m_id, 0)), m_width(std::exchange(other.m_width, 0)),
      m_height(std::exchange(other.m_height, 0)) {}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        destroy();
        m_id = std::exchange(other.m_id, 0);
        m_width = std::exchange(other.m_width, 0);
        m_height = std::exchange(other.m_height, 0);
    }
    return *this;
}

Texture::~Texture() {
    destroy();
}

void Texture::destroy() noexcept {
    if (m_id != 0) {
        glDeleteTextures(1, &m_id);
        m_id = 0;
    }
}

void Texture::bind(unsigned int unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, m_id);
}

} // namespace rb::gl

#include "rabbet/render/gl/Cubemap.h"

#include <glad/glad.h>

#include <utility>

namespace rb::gl {

Cubemap Cubemap::fromFaces(const std::array<Image, 6>& faces) {
    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);

    const int channels = faces[0].channels;
    const GLenum format = channels == 4 ? GL_RGBA : (channels == 1 ? GL_RED : GL_RGB);
    // Colour faces carry display-referred sRGB bytes; declare that so sampling returns
    // linear and the filter blends linearly. GL 4.1 has no core single-channel sRGB
    // format, so a grey face stays R8 and is never decoded.
    const GLint internalFormat =
        channels == 4 ? GL_SRGB8_ALPHA8 : (channels == 1 ? GL_R8 : GL_SRGB8);

    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    for (unsigned int face = 0; face < 6u; ++face) {
        const Image& image = faces[face];
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, internalFormat, image.width,
                     image.height, 0, format, GL_UNSIGNED_BYTE, image.pixels.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return Cubemap(id);
}

Cubemap Cubemap::empty(int size) {
    unsigned int id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_CUBE_MAP, id);
    for (unsigned int face = 0; face < 6u; ++face) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB16F, size, size, 0, GL_RGB,
                     GL_FLOAT, nullptr);
    }
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
    return Cubemap(id);
}

Cubemap::Cubemap(Cubemap&& other) noexcept : m_id(std::exchange(other.m_id, 0)) {}

Cubemap& Cubemap::operator=(Cubemap&& other) noexcept {
    if (this != &other) {
        destroy();
        m_id = std::exchange(other.m_id, 0);
    }
    return *this;
}

Cubemap::~Cubemap() {
    destroy();
}

void Cubemap::destroy() noexcept {
    if (m_id != 0) {
        glDeleteTextures(1, &m_id);
        m_id = 0;
    }
}

void Cubemap::bind(unsigned int unit) const noexcept {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_CUBE_MAP, m_id);
}

} // namespace rb::gl

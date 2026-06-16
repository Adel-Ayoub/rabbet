#pragma once

#include <array>

#include "rabbet/render/Image.h"

namespace rb::gl {

class Cubemap {
public:
    [[nodiscard]] static Cubemap fromFaces(const std::array<Image, 6>& faces);

    // An empty RGB16F cubemap of `size` x `size` faces, allocated but undefined, suitable as a
    // render target (its faces are attached to an FBO to capture a convolved environment). Linear
    // filtering + clamp-to-edge, like fromFaces.
    [[nodiscard]] static Cubemap empty(int size);

    Cubemap(const Cubemap&) = delete;
    Cubemap& operator=(const Cubemap&) = delete;
    Cubemap(Cubemap&& other) noexcept;
    Cubemap& operator=(Cubemap&& other) noexcept;
    ~Cubemap();

    void bind(unsigned int unit = 0) const noexcept;

    [[nodiscard]] unsigned int id() const noexcept { return m_id; }

private:
    explicit Cubemap(unsigned int id) noexcept : m_id(id) {}
    void destroy() noexcept;

    unsigned int m_id = 0;
};

} // namespace rb::gl

#pragma once

namespace rb {

struct Viewport {
    int width = 0;
    int height = 0;

    [[nodiscard]] float aspect() const {
        return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
    }
};

} // namespace rb

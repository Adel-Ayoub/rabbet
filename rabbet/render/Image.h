#pragma once

#include <cstddef>
#include <vector>

namespace rb {

struct Image {
    std::vector<std::byte> pixels;
    int width = 0;
    int height = 0;
    int channels = 0;
};

} // namespace rb

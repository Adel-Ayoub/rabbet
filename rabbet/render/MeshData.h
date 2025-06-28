#pragma once

#include <cstdint>
#include <vector>

#include "rabbet/render/Vertex.h"

namespace rb {

struct MeshData {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
};

} // namespace rb

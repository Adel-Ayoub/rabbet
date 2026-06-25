#include "rabbet/terrain/TerrainMesh.h"

#include <algorithm>
#include <cstdint>

#include <glm/glm.hpp>

namespace rb {

MeshData buildTerrainMesh(const TerrainHeightField& field, float size, float heightScale) {
    MeshData mesh;
    const int res = field.resolution;
    if (res < 2) {
        return mesh;
    }

    const float half = size * 0.5f;
    const float step = std::max(size / static_cast<float>(res - 1), 1e-6f);
    const auto worldHeight = [&](int x, int z) { return field.at(x, z) * heightScale; };

    mesh.vertices.reserve(static_cast<std::size_t>(res) * static_cast<std::size_t>(res));
    for (int z = 0; z < res; ++z) {
        for (int x = 0; x < res; ++x) {
            Vertex vertex;
            vertex.position = {-half + static_cast<float>(x) * step, worldHeight(x, z),
                               -half + static_cast<float>(z) * step};
            // Central-difference gradient -> surface normal. Exact for a uniform grid: a flat field
            // yields exactly +Y, and the edge-clamped sampler keeps the border well-defined.
            const float dhdx = (worldHeight(x + 1, z) - worldHeight(x - 1, z)) / (2.0f * step);
            const float dhdz = (worldHeight(x, z + 1) - worldHeight(x, z - 1)) / (2.0f * step);
            vertex.normal = glm::normalize(glm::vec3(-dhdx, 1.0f, -dhdz));
            vertex.uv = {static_cast<float>(x) / static_cast<float>(res - 1),
                         static_cast<float>(z) / static_cast<float>(res - 1)};
            mesh.vertices.push_back(vertex);
        }
    }

    mesh.indices.reserve(static_cast<std::size_t>(res - 1) * static_cast<std::size_t>(res - 1) * 6u);
    for (int z = 0; z < res - 1; ++z) {
        for (int x = 0; x < res - 1; ++x) {
            const auto i0 = static_cast<std::uint32_t>(z * res + x);
            const auto i1 = static_cast<std::uint32_t>(z * res + (x + 1));
            const auto i2 = static_cast<std::uint32_t>((z + 1) * res + x);
            const auto i3 = static_cast<std::uint32_t>((z + 1) * res + (x + 1));
            // CCW from above so the lit top faces front: see the +Y winding note in the tests.
            mesh.indices.insert(mesh.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    return mesh;
}

} // namespace rb

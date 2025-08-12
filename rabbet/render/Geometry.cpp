#include "rabbet/render/Geometry.h"

#include <cmath>

#include <glm/gtc/constants.hpp>

namespace rb::geometry {

MeshData triangle() {
    MeshData data;
    data.vertices = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}},
    };
    data.indices = {0u, 1u, 2u};
    return data;
}

MeshData quad() {
    MeshData data;
    data.vertices = {
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},
        {{0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},
    };
    data.indices = {0u, 1u, 2u, 2u, 3u, 0u};
    return data;
}

MeshData cube() {
    struct Face {
        glm::vec3 normal;
        glm::vec3 corners[4];
    };

    const Face faces[6] = {
        {{0.0f, 0.0f, 1.0f},
         {{-0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}}},
        {{0.0f, 0.0f, -1.0f},
         {{0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}}},
        {{1.0f, 0.0f, 0.0f},
         {{0.5f, -0.5f, 0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, 0.5f, -0.5f}, {0.5f, 0.5f, 0.5f}}},
        {{-1.0f, 0.0f, 0.0f},
         {{-0.5f, -0.5f, -0.5f}, {-0.5f, -0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, -0.5f}}},
        {{0.0f, 1.0f, 0.0f},
         {{-0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -0.5f}, {-0.5f, 0.5f, -0.5f}}},
        {{0.0f, -1.0f, 0.0f},
         {{-0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, -0.5f}, {0.5f, -0.5f, 0.5f}, {-0.5f, -0.5f, 0.5f}}},
    };

    const glm::vec2 uvs[4] = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}};

    MeshData data;
    data.vertices.reserve(24);
    data.indices.reserve(36);
    for (const Face& face : faces) {
        const auto base = static_cast<std::uint32_t>(data.vertices.size());
        for (int i = 0; i < 4; ++i) {
            data.vertices.push_back({face.corners[i], face.normal, uvs[i]});
        }
        data.indices.insert(data.indices.end(),
                            {base, base + 1u, base + 2u, base + 2u, base + 3u, base});
    }
    return data;
}

MeshData sphere(unsigned int rings, unsigned int sectors) {
    MeshData data;
    const float radius = 0.5f;
    const float pi = glm::pi<float>();
    const float twoPi = glm::two_pi<float>();

    data.vertices.reserve((rings + 1) * (sectors + 1));
    for (unsigned int ring = 0; ring <= rings; ++ring) {
        const float v = static_cast<float>(ring) / static_cast<float>(rings);
        const float phi = v * pi;
        for (unsigned int sector = 0; sector <= sectors; ++sector) {
            const float u = static_cast<float>(sector) / static_cast<float>(sectors);
            const float theta = u * twoPi;
            const glm::vec3 normal{std::sin(phi) * std::cos(theta), std::cos(phi),
                                   std::sin(phi) * std::sin(theta)};
            data.vertices.push_back({radius * normal, normal, glm::vec2{u, v}});
        }
    }

    data.indices.reserve(rings * sectors * 6);
    const unsigned int stride = sectors + 1;
    for (unsigned int ring = 0; ring < rings; ++ring) {
        for (unsigned int sector = 0; sector < sectors; ++sector) {
            const unsigned int i0 = ring * stride + sector;
            const unsigned int i1 = i0 + 1;
            const unsigned int i2 = i0 + stride;
            const unsigned int i3 = i2 + 1;
            data.indices.insert(data.indices.end(), {i0, i2, i1, i1, i2, i3});
        }
    }
    return data;
}

} // namespace rb::geometry

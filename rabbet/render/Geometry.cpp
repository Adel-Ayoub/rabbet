#include "rabbet/render/Geometry.h"

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

} // namespace rb::geometry

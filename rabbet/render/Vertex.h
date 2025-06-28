#pragma once

#include <glm/glm.hpp>

namespace rb {

struct Vertex {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f};
    glm::vec2 uv{0.0f};
};

} // namespace rb

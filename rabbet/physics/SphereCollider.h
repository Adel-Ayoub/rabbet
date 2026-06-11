#pragma once

#include <glm/glm.hpp>

namespace rb {

// A sphere collision shape, in world units. offset is in the entity's local rotated frame.
struct SphereCollider {
    float radius = 0.5f;
    glm::vec3 offset{0.0f};
};

} // namespace rb

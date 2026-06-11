#pragma once

#include <glm/glm.hpp>

namespace rb {

// A box collision shape, in world units (the entity's Transform scale does not scale it).
// halfExtents matches the unit cube mesh when (0.5, 0.5, 0.5); offset is in the entity's
// local rotated frame.
struct BoxCollider {
    glm::vec3 halfExtents{0.5f};
    glm::vec3 offset{0.0f};
};

} // namespace rb

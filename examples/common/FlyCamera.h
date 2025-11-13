#pragma once

#include <glm/glm.hpp>

namespace ex {

// A first-person free-fly camera driven by mouse-look and WASD/QE with momentum.
struct FlyCamera {
    float yaw = 0.0f;
    float pitch = 0.0f;
    glm::vec3 velocity{0.0f};
    float speed = 9.0f;
    float acceleration = 45.0f;
    float damping = 9.0f;
    float sensitivity = 0.0022f;
    float boost = 3.0f;
    bool primed = false;
};

} // namespace ex

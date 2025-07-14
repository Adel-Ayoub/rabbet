#pragma once

#include <glm/glm.hpp>

namespace rb {

struct Camera {
    float fovY = glm::radians(60.0f);
    float nearPlane = 0.1f;
    float farPlane = 100.0f;
};

} // namespace rb

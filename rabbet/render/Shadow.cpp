#include "rabbet/render/Shadow.h"

#include <glm/gtc/matrix_transform.hpp>

namespace rb {

glm::mat4 directionalLightSpace(const glm::vec3& direction, float extent, float nearPlane,
                                float farPlane, float distance) {
    const glm::vec3 forward = glm::normalize(direction);
    const glm::vec3 center{0.0f};
    const glm::vec3 eye = center - forward * distance;

    glm::vec3 up{0.0f, 1.0f, 0.0f};
    if (glm::abs(glm::dot(forward, up)) > 0.99f) {
        up = glm::vec3{0.0f, 0.0f, 1.0f};
    }

    const glm::mat4 view = glm::lookAt(eye, center, up);
    const glm::mat4 projection = glm::ortho(-extent, extent, -extent, extent, nearPlane, farPlane);
    return projection * view;
}

} // namespace rb

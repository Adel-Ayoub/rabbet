#include "rabbet/render/Sky.h"

#include <cmath>

namespace rb {

glm::vec3 skyColor(const glm::vec3& direction) {
    const glm::vec3 dir = glm::normalize(direction);

    const glm::vec3 horizon{0.60f, 0.70f, 0.82f};
    const glm::vec3 zenith{0.16f, 0.32f, 0.60f};
    const glm::vec3 ground{0.28f, 0.26f, 0.24f};

    glm::vec3 color = dir.y >= 0.0f ? glm::mix(horizon, zenith, dir.y)
                                    : glm::mix(horizon, ground, -dir.y);

    const glm::vec3 sunDir = glm::normalize(glm::vec3(0.5f, 0.8f, 0.4f));
    const float sun = std::pow(glm::max(glm::dot(dir, sunDir), 0.0f), 48.0f);
    color += glm::vec3(1.0f, 0.95f, 0.8f) * sun;

    return glm::clamp(color, 0.0f, 1.0f);
}

} // namespace rb

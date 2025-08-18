#pragma once

#include <glm/glm.hpp>

namespace rb {

[[nodiscard]] glm::mat4 directionalLightSpace(const glm::vec3& direction, float extent,
                                              float nearPlane, float farPlane, float distance);

} // namespace rb

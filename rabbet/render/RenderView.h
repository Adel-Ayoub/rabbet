#pragma once

#include <glm/glm.hpp>

namespace rb {

struct RenderView {
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::vec3 position{0.0f};
};

} // namespace rb

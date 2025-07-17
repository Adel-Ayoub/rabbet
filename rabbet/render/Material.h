#pragma once

#include <glm/glm.hpp>

#include "rabbet/render/gl/Texture.h"

namespace rb {

struct Material {
    gl::Texture texture;
    glm::vec3 tint{1.0f};
};

} // namespace rb

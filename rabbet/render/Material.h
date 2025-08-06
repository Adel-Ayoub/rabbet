#pragma once

#include <glm/glm.hpp>

#include "rabbet/render/gl/Texture.h"

namespace rb {

struct Material {
    gl::Texture texture;
    glm::vec3 tint{1.0f};
    float specular = 0.5f;
    float shininess = 32.0f;
};

} // namespace rb

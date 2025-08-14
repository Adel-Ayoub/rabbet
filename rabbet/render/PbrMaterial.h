#pragma once

#include <glm/glm.hpp>

#include "rabbet/render/gl/Texture.h"

namespace rb {

struct PbrMaterial {
    gl::Texture albedo;
    glm::vec3 baseColor{1.0f};
    float metallic = 0.0f;
    float roughness = 0.5f;
    float ao = 1.0f;
};

} // namespace rb

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
    // Trailing so existing positional aggregate inits (albedo/baseColor/metallic/roughness/ao) compile.
    glm::vec3 emissive{0.0f};
};

} // namespace rb

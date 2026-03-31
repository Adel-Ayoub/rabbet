#pragma once

#include <glm/glm.hpp>

namespace rb {

enum class PrimitiveShape { Cube, Sphere, Plane };

// A built-in shape drawn by the RenderSystem from a shared primitive-mesh cache,
// shaded with the PBR shader over a white albedo. Data-only and serializable, so
// entities using it can be created, duplicated, and saved without owning any GL
// resources (unlike a live gl::Mesh component).
struct Primitive {
    PrimitiveShape shape = PrimitiveShape::Cube;
    glm::vec3 color{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.6f;
};

} // namespace rb

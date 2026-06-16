#pragma once

#include <glm/glm.hpp>

namespace rb {

struct DirectionalLight {
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
};

struct PointLight {
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
};

// A cone of light from the entity's Transform position, aimed along `direction`. Shares the
// point-light distance attenuation (constant/linear/quadratic) and adds a soft-edged cone:
// full intensity within the inner half-angle, fading to zero at the outer one. Angles are
// authored in degrees; LightSystem converts them to the cosines the shader compares against.
struct SpotLight {
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
    float innerAngle = 20.0f;
    float outerAngle = 30.0f;
};

// The spot cone as cosines for the shader: x = cos(inner), y = cos(outer), with x >= y. The
// half-angles are clamped to [0, 90) and inner to <= outer so the falloff denominator stays
// positive. Pure; no GL — unit-tested headlessly.
[[nodiscard]] glm::vec2 spotConeCosines(const SpotLight& light) noexcept;

} // namespace rb

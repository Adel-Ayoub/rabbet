#pragma once

#include "rabbet/render/gl/Cubemap.h"

namespace rb {

// Image-based ambient lighting derived from an environment cubemap. `irradiance` is a small
// cosine-convolved cubemap sampled in the surface normal direction to approximate diffuse
// environment light. Disabled by default so a scene renders identically to the flat-ambient
// path until the environment is switched on.
struct EnvironmentLight {
    gl::Cubemap irradiance;
    float intensity = 1.0f;
    bool enabled = false;
};

// Convolves an environment cubemap into a diffuse-irradiance cubemap (cosine-weighted hemisphere
// integral) and returns it wrapped in a disabled EnvironmentLight. Requires a live GL context;
// builds its own transient framebuffer, shader, and cube, and restores the previously bound
// framebuffer + viewport. `size` is the irradiance face resolution (small: it is heavily blurred).
[[nodiscard]] EnvironmentLight buildEnvironmentLight(const gl::Cubemap& environment, int size = 32);

} // namespace rb

#pragma once

#include <glm/glm.hpp>

namespace rb {

// A flat water surface centred on its entity's Transform: the world position sets the level, and
// rotation and scale are ignored so the surface always stays horizontal. A parent's transform
// still moves it (the world position is composed), but a parent's scale does not resize it -
// extent is the only size control. Plain serializable data; the render pass draws one quad with
// procedural waves, so no mesh is built and nothing GL-side lives here.
//
// Not pickable in the viewport (the pick pass draws meshes, and this surface has none), so a
// water entity is selected from the hierarchy. Casts and receives no shadows, like particles.
struct WaterComponent {
    bool enabled = true;
    glm::vec2 extent{30.0f, 30.0f}; // half-size of the surface, world units
    glm::vec4 deepColor{0.05f, 0.14f, 0.20f, 0.85f};
    glm::vec4 shallowColor{0.10f, 0.30f, 0.35f, 0.55f};
    // Wave size and wave steepness are deliberately independent: tile scale sets the wavelength
    // only, and strength alone drives how far the normals tilt.
    float waveTileScale = 0.35f;
    float waveStrength = 0.5f; // normal perturbation, 0 = glass flat
    float waveSpeed = 1.0f;
    float smoothness = 0.9f; // specular sharpness, 0 rough .. 1 mirror
};

// The surface's model matrix: the world translation only (rotation and scale dropped per the
// contract above), scaled to `extent`. Serialized data reaches this unvalidated, so a non-finite
// or degenerate extent collapses to a minimum rather than sending NaN to the rasterizer.
[[nodiscard]] glm::mat4 waterSurfaceModel(const glm::mat4& world, glm::vec2 extent);

// Serialized floats reach the shader unvalidated; a non-finite one paints NaN into the scene
// target, which the bloom chain then spreads. Every field falls back to a sane value.
void sanitizeWater(WaterComponent& water);

} // namespace rb

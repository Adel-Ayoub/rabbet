#pragma once

#include "rabbet/render/Tonemap.h"

namespace rb {

class Scene;

// Scene-authored post-processing settings — the "look" of a scene (exposure, bloom, grade), saved
// as a serialized component so the mood travels with the scene rather than resetting each launch.
// Default-disabled: a scene with no enabled instance renders through the raw forward path unchanged,
// which is the regression-safe seam. The editor drives the post chain from the first enabled one.
struct PostProcess {
    bool enabled = false;

    // Tonemap + exposure + gamma (HDR -> display).
    TonemapOperator tonemap = TonemapOperator::ACES;
    float exposure = 0.0f; // EV stops, applied as 2^ev before tone-mapping
    float gamma = 2.2f;

    // Bloom: a soft-knee bright-pass blurred into a glow, added back before tone-mapping.
    bool bloom = true;
    float bloomThreshold = 1.0f;
    float bloomKnee = 0.5f;
    float bloomIntensity = 0.04f;

    // Colour grade (applied after tone-mapping, before gamma).
    float contrast = 1.0f;   // pivots at mid-grey; 1 = identity
    float saturation = 1.0f; // 1 = identity, 0 = greyscale
    float vignette = 0.0f;   // 0 = off

    bool fxaa = true;
};

// The first enabled PostProcess in the scene, or nullptr. The RenderSystem (to choose the lit-shader
// HDR output) and the editor (to drive the post chain + HDR target) both read this one source, so
// they always agree on whether post-processing is active.
[[nodiscard]] PostProcess* activePostProcess(Scene& scene);

} // namespace rb

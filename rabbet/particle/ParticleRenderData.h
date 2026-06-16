#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/particle/ParticleEmitter.h"

namespace rb {

struct TextureAsset;

// One live particle flattened for the renderer: exactly what a camera-facing billboard needs.
struct ParticleBillboard {
    glm::vec3 position{0.0f};
    float size = 0.0f;
    glm::vec4 color{1.0f};
};

// One emitter's draw batch: a blend mode, an optional sprite, and its live billboards.
struct ParticleDrawBatch {
    ParticleBlendMode blendMode = ParticleBlendMode::Additive;
    AssetHandle<TextureAsset> sprite; // invalid -> the renderer's built-in soft dot
    std::vector<ParticleBillboard> particles;
};

// Published every frame by ParticleSystem, consumed by RenderSystem — the particle analogue of the
// Lighting resource. Cleared and refilled each tick, so an empty list means the render pass is
// skipped entirely (and a scene with no emitter is byte-identical to before).
struct ParticleRenderData {
    std::vector<ParticleDrawBatch> batches;

    void clear() noexcept { batches.clear(); }
};

} // namespace rb

#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"

namespace rb {

struct TextureAsset;

// How a particle's colour is composited over the already-shaded scene. Additive sums light, so it
// is order-independent and reads as glow (fire, sparks, magic); Alpha is a standard over-blend for
// soft, opaque-ish puffs (smoke, dust).
enum class ParticleBlendMode { Additive, Alpha };

// A CPU-simulated particle emitter. Plain, serializable data: the live particle pool lives in the
// ParticleSystem keyed by entity, never in the component, so this stays cheap to copy and save.
// Particles spawn at the entity's Transform position, are aimed by its rotation, and integrate in
// world space; size and colour interpolate from start to end across each particle's life.
struct ParticleEmitter {
    float emissionRate = 24.0f;   // particles spawned per second
    int maxParticles = 256;       // pool capacity; spawns beyond it are dropped (bounds the pool)
    float lifetime = 2.0f;        // base particle lifetime, seconds
    float lifetimeJitter = 0.4f;  // +/- seconds added uniformly, clamped so lifetime stays positive
    float startSize = 0.25f;
    float endSize = 0.0f;
    glm::vec4 startColor{1.0f, 0.75f, 0.30f, 1.0f};
    glm::vec4 endColor{0.9f, 0.18f, 0.05f, 0.0f};
    glm::vec3 velocity{0.0f, 1.6f, 0.0f}; // mean initial velocity, in the emitter's local space
    float coneAngle = 18.0f;              // emission spread half-angle around `velocity`, degrees
    float speedJitter = 0.25f;            // fractional speed variance, sampled in [1-j, 1+j]
    glm::vec3 gravity{0.0f, -1.2f, 0.0f}; // constant acceleration applied each step
    ParticleBlendMode blendMode = ParticleBlendMode::Additive;
    bool looping = true;
    float duration = 5.0f;    // emission window in seconds when not looping (ignored if looping)
    std::uint32_t seed = 1u;  // PRNG seed so a preview / headless sim is reproducible

    Uuid sprite;                      // optional billboard texture; a soft round dot when unset
    AssetHandle<TextureAsset> handle; // runtime resolution of `sprite`, not serialized
};

} // namespace rb

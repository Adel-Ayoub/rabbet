#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rabbet/particle/Particle.h"
#include "rabbet/particle/ParticleRng.h"

namespace rb {

struct ParticleEmitter;

// A unit vector sampled uniformly within `halfAngle` (radians) of `axis`. halfAngle <= 0 returns
// the axis itself. Pure apart from the PRNG it draws from — unit-tested for the cone bound.
[[nodiscard]] glm::vec3 sampleConeDirection(const glm::vec3& axis, float halfAngle,
                                            ParticleRng& rng);

// The CPU particle simulation for one emitter: a fixed pool recycled through an O(1) free list, a
// fractional spawn accumulator, an elapsed clock, and a seeded PRNG. No GL and no Runtime — this is
// the testable unit. Deterministic for a given (seed, dt-sequence): the integrator faithfully uses
// whatever dt it is handed (the runtime system clamps real-world frame spikes before calling in).
class ParticleSimulation {
public:
    // Full reset: sizes the pool to the emitter's capacity, reseeds the PRNG, clears the clocks.
    // step() calls this on first use and whenever the seed changes.
    void configure(const ParticleEmitter& emitter);

    // Advances the simulation by dt: integrate + age live particles, then emit by rate (bounded by
    // the pool). `origin` is the emitter's world position; `orientation` aims the emission cone. A
    // capacity change resizes in place (live particles preserved); a seed change reconfigures.
    void step(const ParticleEmitter& emitter, const glm::vec3& origin,
              const glm::quat& orientation, float dt);

    // Kills every particle and resets the clocks, keeping the pool capacity.
    void clear() noexcept;

    [[nodiscard]] const std::vector<Particle>& pool() const noexcept { return m_pool; }
    [[nodiscard]] std::size_t aliveCount() const noexcept;
    [[nodiscard]] std::size_t freeCount() const noexcept { return m_freeList.size(); }
    [[nodiscard]] float elapsed() const noexcept { return m_elapsed; }

private:
    void resize(std::size_t capacity); // preserves live particles, rebuilds the free list
    void spawnOne(const ParticleEmitter& emitter, const glm::vec3& origin,
                  const glm::quat& orientation);

    std::vector<Particle> m_pool;
    std::vector<std::uint32_t> m_freeList; // indices of dead slots, reused LIFO (O(1) recycle)
    ParticleRng m_rng{1u};
    float m_accumulator = 0.0f; // fractional spawn carry
    float m_elapsed = 0.0f;     // total emitter runtime, seconds
    std::uint32_t m_seed = 1u;  // the seed the pool was configured with
};

} // namespace rb

#include "rabbet/particle/ParticleSimulation.h"

#include "rabbet/particle/ParticleEmitter.h"

#include <algorithm>
#include <cmath>

namespace rb {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;

// Serialized data reaches this without validation; an absurd maxParticles must not
// synchronously allocate gigabytes (terrain clamps resolution the same way).
constexpr std::size_t kMaxPoolCapacity = 65536;

std::size_t poolCapacity(const ParticleEmitter& emitter) noexcept {
    if (emitter.maxParticles <= 0) {
        return 0;
    }
    return std::min<std::size_t>(static_cast<std::size_t>(emitter.maxParticles),
                                 kMaxPoolCapacity);
}

} // namespace

glm::vec3 sampleConeDirection(const glm::vec3& axis, float halfAngle, ParticleRng& rng) {
    const glm::vec3 a =
        glm::length(axis) > 1.0e-5f ? glm::normalize(axis) : glm::vec3(0.0f, 1.0f, 0.0f);
    if (halfAngle <= 0.0f) {
        return a;
    }
    const float cosMax = std::cos(halfAngle);
    const float cosTheta = glm::mix(cosMax, 1.0f, rng.next01()); // in [cosMax, 1] -> within the cone
    const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    const float phi = kTwoPi * rng.next01();

    // A stable tangent basis around the axis (avoid the degenerate cross when axis ~ world up).
    const glm::vec3 up =
        std::fabs(a.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
    const glm::vec3 tangent = glm::normalize(glm::cross(up, a));
    const glm::vec3 bitangent = glm::cross(a, tangent);
    return glm::normalize(sinTheta * std::cos(phi) * tangent + sinTheta * std::sin(phi) * bitangent +
                          cosTheta * a);
}

void ParticleSimulation::configure(const ParticleEmitter& emitter) {
    const std::size_t capacity = poolCapacity(emitter);
    m_pool.assign(capacity, Particle{});
    m_freeList.clear();
    m_freeList.reserve(capacity);
    for (std::size_t i = 0; i < capacity; ++i) {
        m_freeList.push_back(static_cast<std::uint32_t>(capacity - 1 - i));
    }
    m_rng.reseed(emitter.seed);
    m_seed = emitter.seed;
    m_accumulator = 0.0f;
    m_elapsed = 0.0f;
}

void ParticleSimulation::resize(std::size_t capacity) {
    // Preserve the live effect when the cap is dragged in the inspector: keep the surviving slots
    // and rebuild the free list from whatever is now dead (a shrink drops particles in cut slots).
    m_pool.resize(capacity);
    m_freeList.clear();
    m_freeList.reserve(capacity);
    for (std::size_t i = capacity; i-- > 0;) {
        if (!m_pool[i].alive) {
            m_freeList.push_back(static_cast<std::uint32_t>(i));
        }
    }
}

void ParticleSimulation::clear() noexcept {
    m_freeList.clear();
    for (std::size_t i = m_pool.size(); i-- > 0;) {
        m_pool[i].alive = false;
        m_freeList.push_back(static_cast<std::uint32_t>(i));
    }
    m_accumulator = 0.0f;
    m_elapsed = 0.0f;
}

void ParticleSimulation::spawnOne(const ParticleEmitter& emitter, const glm::vec3& origin,
                                  const glm::quat& orientation) {
    if (m_freeList.empty()) {
        return; // pool full: drop the spawn, keeping the pool bounded
    }
    const std::uint32_t slot = m_freeList.back();
    m_freeList.pop_back();
    Particle& p = m_pool[slot];

    const float jitter = emitter.lifetimeJitter > 0.0f
                             ? m_rng.range(-emitter.lifetimeJitter, emitter.lifetimeJitter)
                             : 0.0f;
    p.lifetime = std::max(0.01f, emitter.lifetime + jitter);
    p.age = 0.0f;
    p.alive = true;
    p.position = origin;

    const float speed = glm::length(emitter.velocity);
    const glm::vec3 localDir =
        speed > 1.0e-5f ? emitter.velocity / speed : glm::vec3(0.0f, 1.0f, 0.0f);
    const glm::vec3 coneDir = sampleConeDirection(localDir, glm::radians(emitter.coneAngle), m_rng);
    const float speedScale = emitter.speedJitter > 0.0f
                                 ? std::max(0.0f, m_rng.range(1.0f - emitter.speedJitter,
                                                              1.0f + emitter.speedJitter))
                                 : 1.0f;
    p.velocity = (orientation * coneDir) * (speed * speedScale);

    p.size = emitter.startSize;
    p.color = emitter.startColor;
}

void ParticleSimulation::step(const ParticleEmitter& emitter, const glm::vec3& origin,
                              const glm::quat& orientation, float dt) {
    const std::size_t capacity = poolCapacity(emitter);
    if (m_seed != emitter.seed) {
        configure(emitter); // a new seed is an explicit request for a fresh random stream
    } else if (m_pool.size() != capacity) {
        resize(capacity);
    }
    if (dt < 0.0f) {
        dt = 0.0f;
    }

    for (std::size_t i = 0; i < m_pool.size(); ++i) {
        Particle& p = m_pool[i];
        if (!p.alive) {
            continue;
        }
        p.age += dt;
        if (p.age >= p.lifetime) {
            p.alive = false;
            m_freeList.push_back(static_cast<std::uint32_t>(i));
            continue;
        }
        p.velocity += emitter.gravity * dt;
        p.position += p.velocity * dt;
        const float t = p.lifetime > 0.0f ? p.age / p.lifetime : 1.0f;
        p.size = glm::mix(emitter.startSize, emitter.endSize, t);
        p.color = glm::mix(emitter.startColor, emitter.endColor, t);
    }

    const bool emitting = emitter.looping || m_elapsed < emitter.duration;
    if (emitting && emitter.emissionRate > 0.0f && capacity > 0) {
        m_accumulator += emitter.emissionRate * dt;
        // A single step can fill at most the whole pool; clamping bounds the spawn loop so a large
        // dt can't churn a full pool thousands of times for nothing.
        m_accumulator = std::min(m_accumulator, static_cast<float>(capacity));
        while (m_accumulator >= 1.0f) {
            m_accumulator -= 1.0f;
            spawnOne(emitter, origin, orientation);
        }
    } else {
        m_accumulator = 0.0f;
    }

    m_elapsed += dt;
}

std::size_t ParticleSimulation::aliveCount() const noexcept {
    std::size_t alive = 0;
    for (const Particle& p : m_pool) {
        if (p.alive) {
            ++alive;
        }
    }
    return alive;
}

} // namespace rb

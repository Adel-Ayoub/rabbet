#include "rabbet/particle/ParticleSimulation.h"

#include "rabbet/particle/ParticleEmitter.h"

#include <algorithm>
#include <cmath>

namespace rb {
namespace {

constexpr float kTwoPi = 6.28318530717958647692f;
constexpr float kPi = 3.14159265358979323846f;

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

// Serialized junk reaches this unvalidated (the kMaxPoolCapacity discipline): a non-finite size
// reads as zero and anything past the cap clamps, so no birth can be NaN-poisoned.
constexpr float kMaxShapeSize = 1.0e6f;

float shapeSize(float value) noexcept {
    if (!std::isfinite(value) || value <= 0.0f) {
        return 0.0f;
    }
    return std::min(value, kMaxShapeSize);
}

// NaN sails through std::clamp (both comparisons false), so gate finiteness first; a junk
// thickness reads as the solid default, like a missing field.
float thickness01(float value) noexcept {
    if (!std::isfinite(value)) {
        return 1.0f;
    }
    return std::clamp(value, 0.0f, 1.0f);
}

// A birth offset in the emitter's local space. Point must not touch the PRNG so pre-shape content
// replays its exact particle streams. Band radii go through the inverse CDF (cbrt in a sphere,
// sqrt in a disc) on the inner/outer fraction, giving uniform density without cubing a large
// radius past float range. One draw per statement: argument evaluation order is unspecified and
// the stream contract is cross-platform.
glm::vec3 sampleShapeOffset(const ParticleEmitter& emitter, ParticleRng& rng) {
    switch (emitter.shape) {
    case ParticleEmissionShape::Point:
        return glm::vec3(0.0f);
    case ParticleEmissionShape::Sphere: {
        const float outer = shapeSize(emitter.shapeRadius);
        const float f = 1.0f - thickness01(emitter.shapeThickness);
        const glm::vec3 dir = sampleConeDirection(glm::vec3(0.0f, 1.0f, 0.0f), kPi, rng);
        const float r = outer * std::cbrt(glm::mix(f * f * f, 1.0f, rng.next01()));
        return dir * r;
    }
    case ParticleEmissionShape::Box: {
        const glm::vec3 ext(shapeSize(emitter.shapeExtents.x), shapeSize(emitter.shapeExtents.y),
                            shapeSize(emitter.shapeExtents.z));
        const float x = rng.range(-ext.x, ext.x);
        const float y = rng.range(-ext.y, ext.y);
        const float z = rng.range(-ext.z, ext.z);
        return glm::vec3(x, y, z);
    }
    case ParticleEmissionShape::Ring: {
        const float outer = shapeSize(emitter.shapeRadius);
        const float f = 1.0f - thickness01(emitter.shapeThickness);
        const float r = outer * std::sqrt(glm::mix(f * f, 1.0f, rng.next01()));
        const float phi = kTwoPi * rng.next01();
        return glm::vec3(r * std::cos(phi), 0.0f, r * std::sin(phi));
    }
    }
    return glm::vec3(0.0f);
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

    const glm::vec3 offset = sampleShapeOffset(emitter, m_rng);
    p.position = origin + orientation * offset;

    const float speed = glm::length(emitter.velocity);
    const glm::vec3 localDir =
        speed > 1.0e-5f ? emitter.velocity / speed : glm::vec3(0.0f, 1.0f, 0.0f);
    // The cone is drawn even when the radial direction wins, so the per-birth draw count never
    // depends on sampled data; a last-ulp offsetLength flip would otherwise desync the stream.
    const glm::vec3 coneDir = sampleConeDirection(localDir, glm::radians(emitter.coneAngle), m_rng);
    const float offsetLength = glm::length(offset);
    // A birth at the exact centre has no outward direction, so it takes the cone like Point does.
    const glm::vec3 localBirthDir =
        emitter.emitOutward && offsetLength > 1.0e-5f ? offset / offsetLength : coneDir;
    const float speedScale = emitter.speedJitter > 0.0f
                                 ? std::max(0.0f, m_rng.range(1.0f - emitter.speedJitter,
                                                              1.0f + emitter.speedJitter))
                                 : 1.0f;
    p.velocity = (orientation * localBirthDir) * (speed * speedScale);

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

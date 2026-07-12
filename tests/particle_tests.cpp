#include "rabbet/particle/Particle.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/particle/ParticleRng.h"
#include "rabbet/particle/ParticleSimulation.h"
#include "rabbet/particle/ParticleRenderData.h"
#include "rabbet/particle/ParticleSystem.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstddef>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

// A determinism-friendly emitter: no jitter, no spread, infinite life — so spawn/age/integrate are
// exactly predictable. Individual tests tweak the fields they exercise.
rb::ParticleEmitter steadyEmitter() {
    rb::ParticleEmitter e;
    e.emissionRate = 10.0f;
    e.maxParticles = 100;
    e.lifetime = 1000.0f;
    e.lifetimeJitter = 0.0f;
    e.coneAngle = 0.0f;
    e.speedJitter = 0.0f;
    e.gravity = glm::vec3(0.0f);
    e.velocity = glm::vec3(0.0f, 1.0f, 0.0f);
    e.startSize = 1.0f;
    e.endSize = 1.0f;
    e.startColor = glm::vec4(1.0f);
    e.endColor = glm::vec4(1.0f);
    e.looping = true;
    return e;
}

const rb::Particle* firstAlive(const rb::ParticleSimulation& sim) {
    for (const rb::Particle& p : sim.pool()) {
        if (p.alive) {
            return &p;
        }
    }
    return nullptr;
}

// The PRNG is reproducible: same seed -> identical stream; a different seed diverges.
void rngIsDeterministicPerSeed() {
    rb::ParticleRng a(42u);
    rb::ParticleRng b(42u);
    bool identical = true;
    for (int i = 0; i < 256; ++i) {
        if (a.nextU32() != b.nextU32()) {
            identical = false;
        }
    }
    CHECK(identical);

    rb::ParticleRng c(42u);
    rb::ParticleRng d(43u);
    bool diverged = false;
    for (int i = 0; i < 256; ++i) {
        if (c.nextU32() != d.nextU32()) {
            diverged = true;
        }
    }
    CHECK(diverged);

    rb::ParticleRng u(7u);
    for (int i = 0; i < 1000; ++i) {
        const float v = u.next01();
        CHECK(v >= 0.0f);
        CHECK(v < 1.0f);
    }
}

// Every sampled direction lies within the cone half-angle of the axis; a zero angle is exact.
void coneDirectionsStayWithinHalfAngle() {
    rb::ParticleRng rng(123u);
    const glm::vec3 axis = glm::normalize(glm::vec3(0.2f, 1.0f, -0.3f));
    const float half = glm::radians(20.0f);
    const float cosHalf = std::cos(half);
    bool allInside = true;
    for (int i = 0; i < 2000; ++i) {
        const glm::vec3 dir = rb::sampleConeDirection(axis, half, rng);
        CHECK(approx(glm::length(dir), 1.0f, 1.0e-3f));
        if (glm::dot(dir, axis) < cosHalf - 1.0e-4f) {
            allInside = false;
        }
    }
    CHECK(allInside);

    const glm::vec3 exact = rb::sampleConeDirection(axis, 0.0f, rng);
    CHECK(approx(glm::dot(exact, axis), 1.0f));
}

// Rate * dt spawns accumulate as whole particles; the count grows deterministically.
void spawnsByRate() {
    rb::ParticleEmitter e = steadyEmitter();
    e.emissionRate = 10.0f;
    rb::ParticleSimulation sim;

    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f);
    CHECK(sim.aliveCount() == 10u);
    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f);
    CHECK(sim.aliveCount() == 20u);

    // A fractional carry only spawns once it crosses a whole particle.
    rb::ParticleSimulation slow;
    rb::ParticleEmitter trickle = steadyEmitter();
    trickle.emissionRate = 2.0f;
    slow.step(trickle, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.4f); // 0.8 accumulated -> 0 spawns
    CHECK(slow.aliveCount() == 0u);
    slow.step(trickle, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.4f); // 1.6 accumulated -> 1 spawn
    CHECK(slow.aliveCount() == 1u);
}

// The pool never exceeds its capacity no matter how hard the emitter pushes, and alive + free
// always accounts for exactly the pool.
void poolStaysBounded() {
    rb::ParticleEmitter e = steadyEmitter();
    e.emissionRate = 100000.0f;
    e.maxParticles = 8;
    rb::ParticleSimulation sim;
    for (int i = 0; i < 50; ++i) {
        sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.1f);
        CHECK(sim.aliveCount() <= 8u);
        CHECK(sim.aliveCount() + sim.freeCount() == 8u);
    }
    CHECK(sim.aliveCount() == 8u); // saturated
}

// Dead particles are recycled into the fixed pool: the alive count holds at the cap and the pool
// vector never grows.
void deadParticlesRecycle() {
    rb::ParticleEmitter e = steadyEmitter();
    e.emissionRate = 100000.0f; // fill immediately
    e.maxParticles = 4;
    e.lifetime = 1.0f;
    rb::ParticleSimulation sim;

    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.5f);
    CHECK(sim.aliveCount() == 4u);
    CHECK(sim.pool().size() == 4u);

    // After more than a lifetime the first wave dies and is immediately replaced from the pool.
    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.6f);
    CHECK(sim.aliveCount() == 4u);
    CHECK(sim.pool().size() == 4u);
}

// Velocity integrates under gravity and position integrates under velocity (semi-implicit Euler).
void integratesUnderGravity() {
    rb::ParticleEmitter e = steadyEmitter();
    e.emissionRate = 1.0f;
    e.velocity = glm::vec3(2.0f, 0.0f, 0.0f);
    e.gravity = glm::vec3(0.0f, -10.0f, 0.0f);
    rb::ParticleSimulation sim;

    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f); // spawn one at origin, vel (2,0,0)
    const rb::Particle* spawned = firstAlive(sim);
    CHECK(spawned != nullptr);
    if (spawned != nullptr) {
        CHECK(approx(spawned->position.x, 0.0f));
        CHECK(approx(spawned->velocity.x, 2.0f));
        CHECK(approx(spawned->velocity.y, 0.0f));
    }

    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.5f); // integrate that particle
    const rb::Particle* moved = firstAlive(sim);
    CHECK(moved != nullptr);
    if (moved != nullptr) {
        CHECK(approx(moved->velocity.x, 2.0f));
        CHECK(approx(moved->velocity.y, -5.0f)); // += gravity * dt
        CHECK(approx(moved->position.x, 1.0f));  // += velocity * dt
        CHECK(approx(moved->position.y, -2.5f));
    }
}

// Size and colour interpolate from start to end across the particle's life.
void sizeAndColourInterpolate() {
    rb::ParticleEmitter e = steadyEmitter();
    e.emissionRate = 1.0f;
    e.lifetime = 1.0f;
    e.startSize = 1.0f;
    e.endSize = 0.0f;
    e.startColor = glm::vec4(1.0f, 0.0f, 0.0f, 1.0f);
    e.endColor = glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
    rb::ParticleSimulation sim;

    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f); // spawn (age 0 -> start values)
    const rb::Particle* born = firstAlive(sim);
    CHECK(born != nullptr);
    if (born != nullptr) {
        CHECK(approx(born->size, 1.0f));
        CHECK(approx(born->color.r, 1.0f));
        CHECK(approx(born->color.a, 1.0f));
    }

    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.5f); // half-life -> midpoint
    const rb::Particle* mid = firstAlive(sim);
    CHECK(mid != nullptr);
    if (mid != nullptr) {
        CHECK(approx(mid->size, 0.5f));
        CHECK(approx(mid->color.r, 0.5f));
        CHECK(approx(mid->color.b, 0.5f));
        CHECK(approx(mid->color.a, 0.5f));
    }
}

// A non-looping emitter stops spawning once its duration elapses; existing particles live on.
void nonLoopingStopsAfterDuration() {
    rb::ParticleEmitter e = steadyEmitter();
    e.looping = false;
    e.duration = 1.0f;
    e.emissionRate = 10.0f;
    rb::ParticleSimulation sim;

    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.5f); // emitting -> 5
    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.5f); // emitting -> 10
    CHECK(sim.aliveCount() == 10u);
    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.5f); // window closed -> still 10
    CHECK(sim.aliveCount() == 10u);
}

// Degenerate emitters are inert: zero rate or zero capacity spawn nothing.
void degenerateEmittersSpawnNothing() {
    rb::ParticleSimulation noRate;
    rb::ParticleEmitter a = steadyEmitter();
    a.emissionRate = 0.0f;
    noRate.step(a, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 5.0f);
    CHECK(noRate.aliveCount() == 0u);

    rb::ParticleSimulation noPool;
    rb::ParticleEmitter b = steadyEmitter();
    b.maxParticles = 0;
    noPool.step(b, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 5.0f);
    CHECK(noPool.aliveCount() == 0u);
    CHECK(noPool.pool().empty());
}

// Growing the cap keeps the live effect; shrinking it bounds the pool to the new size.
void capacityResizePreservesLiveParticles() {
    rb::ParticleEmitter e = steadyEmitter();
    e.emissionRate = 100000.0f;
    e.maxParticles = 8;
    rb::ParticleSimulation sim;
    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.1f);
    CHECK(sim.aliveCount() == 8u);

    e.maxParticles = 16; // grow: survivors preserved, dt 0 so nothing new
    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.0f);
    CHECK(sim.pool().size() == 16u);
    CHECK(sim.aliveCount() == 8u);

    e.maxParticles = 2; // shrink: pool bounded to the new cap
    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.0f);
    CHECK(sim.pool().size() == 2u);
    CHECK(sim.aliveCount() <= 2u);
}

// Changing the seed reconfigures the simulation (a fresh, reproducible stream).
void seedChangeReconfigures() {
    rb::ParticleEmitter e = steadyEmitter();
    e.emissionRate = 100000.0f;
    e.maxParticles = 8;
    rb::ParticleSimulation sim;
    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.1f);
    CHECK(sim.aliveCount() == 8u);

    e.seed = 99u;
    sim.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 0.0f); // reconfigure, dt 0 -> empty
    CHECK(sim.aliveCount() == 0u);
}

// A whole run is reproducible: identical emitters stepped identically end identically.
void identicalRunsMatch() {
    rb::ParticleEmitter e = steadyEmitter();
    e.emissionRate = 30.0f;
    e.lifetime = 0.8f;
    e.lifetimeJitter = 0.3f;
    e.coneAngle = 25.0f;
    e.speedJitter = 0.4f;
    e.gravity = glm::vec3(0.0f, -2.0f, 0.0f);

    rb::ParticleSimulation a;
    rb::ParticleSimulation b;
    for (int i = 0; i < 120; ++i) {
        a.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);
        b.step(e, glm::vec3(0.0f), glm::quat(1, 0, 0, 0), 1.0f / 60.0f);
    }
    CHECK(a.aliveCount() == b.aliveCount());
    bool poolsMatch = a.pool().size() == b.pool().size();
    for (std::size_t i = 0; i < a.pool().size() && poolsMatch; ++i) {
        if (a.pool()[i].alive != b.pool()[i].alive ||
            !approx(a.pool()[i].position.y, b.pool()[i].position.y)) {
            poolsMatch = false;
        }
    }
    CHECK(poolsMatch);
}

// The emitter round-trips through the scene serializer with every field intact.
void emitterRoundTrips() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity e = source.create();
    rb::ParticleEmitter emitter;
    emitter.emissionRate = 33.0f;
    emitter.maxParticles = 512;
    emitter.lifetime = 2.5f;
    emitter.lifetimeJitter = 0.7f;
    emitter.startSize = 0.4f;
    emitter.endSize = 0.05f;
    emitter.startColor = glm::vec4(0.2f, 0.4f, 0.6f, 0.8f);
    emitter.endColor = glm::vec4(0.1f, 0.0f, 0.0f, 0.0f);
    emitter.velocity = glm::vec3(1.0f, 3.0f, -1.0f);
    emitter.coneAngle = 27.0f;
    emitter.speedJitter = 0.33f;
    emitter.gravity = glm::vec3(0.0f, -4.0f, 0.0f);
    emitter.blendMode = rb::ParticleBlendMode::Alpha;
    emitter.looping = false;
    emitter.duration = 3.5f;
    emitter.seed = 7u;
    emitter.sprite = rb::Uuid::generate();
    source.add<rb::ParticleEmitter>(e, emitter);

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    CHECK(loaded.count<rb::ParticleEmitter>() == 1u);
    loaded.each<rb::ParticleEmitter>([&](rb::Entity, rb::ParticleEmitter& l) {
        CHECK(approx(l.emissionRate, 33.0f));
        CHECK(l.maxParticles == 512);
        CHECK(approx(l.lifetime, 2.5f));
        CHECK(approx(l.lifetimeJitter, 0.7f));
        CHECK(approx(l.endSize, 0.05f));
        CHECK(approx(l.startColor.b, 0.6f));
        CHECK(approx(l.endColor.a, 0.0f));
        CHECK(approx(l.velocity.z, -1.0f));
        CHECK(approx(l.coneAngle, 27.0f));
        CHECK(approx(l.speedJitter, 0.33f));
        CHECK(approx(l.gravity.y, -4.0f));
        CHECK(l.blendMode == rb::ParticleBlendMode::Alpha);
        CHECK(l.looping == false);
        CHECK(approx(l.duration, 3.5f));
        CHECK(l.seed == 7u);
        CHECK(l.sprite == emitter.sprite);
        CHECK(!l.handle.valid()); // runtime handle is never serialized
    });
}

// A partial emitter (missing keys) loads with defaults instead of throwing.
void partialEmitterTakesDefaults() {
    const nlohmann::json partial = {{"emissionRate", 12.0f}, {"blendMode", "Alpha"}};
    const auto emitter = partial.get<rb::ParticleEmitter>();
    CHECK(approx(emitter.emissionRate, 12.0f));
    CHECK(emitter.blendMode == rb::ParticleBlendMode::Alpha);
    CHECK(emitter.maxParticles == 256); // default preserved
    CHECK(approx(emitter.lifetime, 2.0f));
    CHECK(!emitter.sprite.valid());
}

} // namespace

// The system spawns a parented emitter's particles at its composed world position, not
// its local offset (zero velocity and gravity keep newborns exactly where they were born).
static void parentedEmitterSpawnsAtItsWorldPosition() {
    rb::Runtime rt;
    rt.addSystem<rb::ParticleSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity rig = scene.create();
    rb::Transform rigPose;
    rigPose.position = glm::vec3(10.0f, 0.0f, 0.0f);
    scene.add<rb::Transform>(rig, rigPose);

    const rb::Entity sparks = scene.create();
    rb::Transform local;
    local.position = glm::vec3(1.0f, 0.0f, 0.0f);
    scene.add<rb::Transform>(sparks, local);
    rb::ParticleEmitter emitter;
    emitter.emissionRate = 200.0f;
    emitter.velocity = glm::vec3(0.0f);
    emitter.speedJitter = 0.0f;
    emitter.gravity = glm::vec3(0.0f);
    scene.add<rb::ParticleEmitter>(sparks, emitter);
    CHECK(rb::setParent(scene, sparks, rig));

    rt.start();
    rt.tick(0.05f);
    rt.stop();

    const rb::ParticleRenderData* data = rt.tryResource<rb::ParticleRenderData>();
    CHECK(data != nullptr);
    const bool spawned =
        data != nullptr && !data->batches.empty() && !data->batches.front().particles.empty();
    CHECK(spawned);
    if (spawned) {
        const glm::vec3 p = data->batches.front().particles.front().position;
        CHECK(std::fabs(p.x - 11.0f) < 1.0e-3f);
        CHECK(std::fabs(p.y) < 1.0e-3f);
    }
}

// Serialized data reaches the sim unvalidated: an absurd maxParticles clamps to the pool
// cap instead of synchronously allocating gigabytes on first sight of the emitter.
void hugeCapacityClamps() {
    rb::ParticleEmitter emitter;
    emitter.maxParticles = 1000000000;
    rb::ParticleSimulation sim;
    sim.configure(emitter);
    CHECK(sim.pool().size() == 65536u);
    CHECK(sim.freeCount() == 65536u);
}

int main() {
    rngIsDeterministicPerSeed();
    coneDirectionsStayWithinHalfAngle();
    spawnsByRate();
    poolStaysBounded();
    deadParticlesRecycle();
    integratesUnderGravity();
    sizeAndColourInterpolate();
    nonLoopingStopsAfterDuration();
    degenerateEmittersSpawnNothing();
    capacityResizePreservesLiveParticles();
    seedChangeReconfigures();
    identicalRunsMatch();
    emitterRoundTrips();
    partialEmitterTakesDefaults();
    parentedEmitterSpawnsAtItsWorldPosition();
    hugeCapacityClamps();
    return rbtest::summary("particle");
}

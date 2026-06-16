#include "rabbet/particle/ParticleSystem.h"

#include <algorithm>

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/particle/ParticleRenderData.h"
#include "rabbet/scene/Transform.h"

namespace rb {
namespace {

// Clamp a real-world frame delta before stepping so an editor stall (window unfocused, a long
// load) replays as a slow frame rather than teleporting every particle or bursting the pool. The
// pure simulation stays faithful to whatever dt it is handed; this guard lives in the runtime.
constexpr float kMaxStep = 0.1f;

} // namespace

void ParticleSystem::reap(Scene& scene) {
    // Drop pools whose emitter was removed or whose entity was destroyed, so a spawn/destroy loop
    // can't grow the simulation set without bound (mirrors the AudioSystem voice reaping).
    for (auto it = m_sims.begin(); it != m_sims.end();) {
        if (!scene.alive(it->first) || scene.tryGet<ParticleEmitter>(it->first) == nullptr) {
            it = m_sims.erase(it);
        } else {
            ++it;
        }
    }
}

void ParticleSystem::onUpdate(Runtime& runtime, float dt) {
    Scene& scene = runtime.scene();

    if (scene.count<ParticleEmitter>() == 0) {
        // Nothing to simulate: keep the resource (if any) empty so the render pass stays dormant
        // and a scene with no emitter is byte-identical to before this phase.
        if (ParticleRenderData* data = runtime.tryResource<ParticleRenderData>()) {
            data->clear();
        }
        m_sims.clear();
        return;
    }

    reap(scene);

    if (!runtime.hasResource<ParticleRenderData>()) {
        runtime.addResource<ParticleRenderData>();
    }
    ParticleRenderData& data = runtime.resource<ParticleRenderData>();
    data.clear();

    const float step = std::min(std::max(dt, 0.0f), kMaxStep);

    scene.each<ParticleEmitter, Transform>(
        [&](Entity entity, ParticleEmitter& emitter, Transform& transform) {
            ParticleSimulation& sim = m_sims[entity];
            sim.step(emitter, transform.position, transform.rotation, step);

            ParticleDrawBatch batch;
            batch.blendMode = emitter.blendMode;
            // The sprite handle is resolved by the AssetResolveSystem (like ModelRenderer); this
            // system stays GL-free and just forwards whatever has resolved. An invalid handle (no
            // sprite, or not yet imported) renders as the built-in soft dot.
            batch.sprite = emitter.sprite.valid() ? emitter.handle : AssetHandle<TextureAsset>{};
            batch.particles.reserve(sim.aliveCount());
            for (const Particle& p : sim.pool()) {
                if (p.alive) {
                    batch.particles.push_back(ParticleBillboard{p.position, p.size, p.color});
                }
            }
            if (!batch.particles.empty()) {
                data.batches.push_back(std::move(batch));
            }
        });
}

} // namespace rb

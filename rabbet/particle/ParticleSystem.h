#pragma once

#include <unordered_map>

#include "rabbet/core/System.h"
#include "rabbet/ecs/Entity.h"
#include "rabbet/particle/ParticleSimulation.h"

namespace rb {

class Scene;

// Steps every ParticleEmitter's CPU simulation each frame and publishes the live particles into a
// ParticleRenderData resource for the RenderSystem. Runs in the Always phase so emitters preview in
// the editor without entering Play (like lights): the simulation only mutates this system's private
// per-entity pools and the transient render resource — never the scene or its components — so Play
// snapshot/restore is unaffected, and a scene with no emitter does no work at all.
class ParticleSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;

private:
    void reap(Scene& scene);

    std::unordered_map<Entity, ParticleSimulation> m_sims;
};

} // namespace rb

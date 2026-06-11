#pragma once

#include <memory>

#include "rabbet/core/System.h"

namespace rb {

// Steps a Jolt physics world during Play. On the play-session begin edge it builds bodies
// from entities that have a RigidBody plus a Box/SphereCollider, using their current
// Transform; each tick it advances the simulation and writes dynamic/kinematic body
// transforms back to the entities; on play end it tears the world down. Jolt lives behind
// the impl, so this header — and the rest of the engine and editor — never include it.
class PhysicsSystem final : public System {
public:
    PhysicsSystem();
    ~PhysicsSystem() override;

    PhysicsSystem(const PhysicsSystem&) = delete;
    PhysicsSystem& operator=(const PhysicsSystem&) = delete;
    PhysicsSystem(PhysicsSystem&&) = delete;
    PhysicsSystem& operator=(PhysicsSystem&&) = delete;

    void onUpdate(Runtime& runtime, float dt) override;
    void onPlayBegin(Runtime& runtime) override;
    void onPlayEnd(Runtime& runtime) override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rb

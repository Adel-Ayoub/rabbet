#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/PhysicsControl.h"
#include "rabbet/physics/PhysicsSystem.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scripting/ScriptAsset.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptSystem.h"
#include "tests/Test.h"

#include <glm/glm.hpp>

#include <cmath>

namespace {

constexpr float kStep = 1.0f / 60.0f;

rb::Entity makeBox(rb::Runtime& runtime, const glm::vec3& position, rb::BodyType type,
                   bool gravity = true) {
    const rb::Entity e = runtime.scene().create();
    rb::Transform transform;
    transform.position = position;
    runtime.scene().add<rb::Transform>(e, transform);
    runtime.scene().add<rb::RigidBody>(e, rb::RigidBody{type, 1.0f, 0.4f, 0.0f, gravity});
    runtime.scene().add<rb::BoxCollider>(e, rb::BoxCollider{glm::vec3(0.5f), glm::vec3(0.0f)});
    return e;
}

float posY(rb::Runtime& runtime, rb::Entity e) {
    return runtime.scene().get<rb::Transform>(e).position.y;
}

// A dynamic box dropped above a static floor falls under gravity and comes to rest on top
// of it (centre ~1.0: floor top 0.5 + box half-extent 0.5), without tunnelling through.
void dynamicFallsAndLands() {
    rb::Runtime runtime;
    rb::PhysicsSystem physics;

    const rb::Entity floor = runtime.scene().create();
    runtime.scene().add<rb::Transform>(floor, rb::Transform{});
    runtime.scene().add<rb::RigidBody>(floor,
                                       rb::RigidBody{rb::BodyType::Static, 0.0f, 0.4f, 0.0f, true});
    runtime.scene().add<rb::BoxCollider>(
        floor, rb::BoxCollider{glm::vec3(5.0f, 0.5f, 5.0f), glm::vec3(0.0f)});

    const rb::Entity box = makeBox(runtime, glm::vec3(0.0f, 5.0f, 0.0f), rb::BodyType::Dynamic);
    const float startY = posY(runtime, box);

    physics.onPlayBegin(runtime);
    for (int i = 0; i < 180; ++i) {
        physics.onUpdate(runtime, kStep);
    }
    const float endY = posY(runtime, box);

    CHECK(endY < startY);  // fell
    CHECK(endY > 0.6f);    // did not tunnel through the floor
    CHECK(endY < 1.4f);    // settled near the floor top
}

// A static body is never moved by the simulation.
void staticStaysPut() {
    rb::Runtime runtime;
    rb::PhysicsSystem physics;
    const rb::Entity e = makeBox(runtime, glm::vec3(0.0f, 3.0f, 0.0f), rb::BodyType::Static);

    physics.onPlayBegin(runtime);
    for (int i = 0; i < 60; ++i) {
        physics.onUpdate(runtime, kStep);
    }
    CHECK(posY(runtime, e) == 3.0f);
}

// gravity = false: a dynamic body floats in place.
void noGravityFloats() {
    rb::Runtime runtime;
    rb::PhysicsSystem physics;
    const rb::Entity e =
        makeBox(runtime, glm::vec3(0.0f, 3.0f, 0.0f), rb::BodyType::Dynamic, false);

    physics.onPlayBegin(runtime);
    for (int i = 0; i < 60; ++i) {
        physics.onUpdate(runtime, kStep);
    }
    CHECK(std::fabs(posY(runtime, e) - 3.0f) < 0.05f);
}

// onPlayEnd tears the world down; a fresh onPlayBegin rebuilds bodies from the current
// transforms, so a new session simulates again from the (restored) start position.
void rebuildsBetweenSessions() {
    rb::Runtime runtime;
    rb::PhysicsSystem physics;
    const rb::Entity box = makeBox(runtime, glm::vec3(0.0f, 5.0f, 0.0f), rb::BodyType::Dynamic);

    physics.onPlayBegin(runtime);
    for (int i = 0; i < 60; ++i) {
        physics.onUpdate(runtime, kStep);
    }
    CHECK(posY(runtime, box) < 5.0f);
    physics.onPlayEnd(runtime);

    runtime.scene().get<rb::Transform>(box).position.y = 5.0f; // editor restores the snapshot
    physics.onPlayBegin(runtime);
    for (int i = 0; i < 30; ++i) {
        physics.onUpdate(runtime, kStep);
    }
    const float afterReset = posY(runtime, box);
    CHECK(afterReset < 4.95f); // fell again -> the world was rebuilt
    CHECK(afterReset > 3.0f);
}

// The fixed-timestep accumulator: a sub-step-sized dt does not advance the sim yet, and a
// huge dt is capped (not integrated as one giant freefall) so a hitch can't explode the sim.
void accumulatorGatesAndCaps() {
    rb::Runtime gate;
    rb::PhysicsSystem pg;
    const rb::Entity bg = makeBox(gate, glm::vec3(0.0f, 5.0f, 0.0f), rb::BodyType::Dynamic);
    pg.onPlayBegin(gate);
    pg.onUpdate(gate, 1.0f / 600.0f); // below the 1/60 fixed step -> no substep
    CHECK(posY(gate, bg) == 5.0f);

    rb::Runtime cap;
    rb::PhysicsSystem pc;
    const rb::Entity bc = makeBox(cap, glm::vec3(0.0f, 5.0f, 0.0f), rb::BodyType::Dynamic);
    pc.onPlayBegin(cap);
    pc.onUpdate(cap, 10.0f); // one giant frame: capped to a few substeps, not 10s of freefall
    const float dropped = 5.0f - posY(cap, bc);
    CHECK(dropped > 0.0f);
    CHECK(dropped < 1.0f);
}

// A queued SetVelocity command drives the body that tick, the queue drains, the stepped
// velocity is published in PhysicsState, and onPlayEnd clears the bridge.
void velocityCommandDrivesBodyAndPublishes() {
    rb::Runtime runtime;
    rb::PhysicsSystem physics;
    const rb::Entity box =
        makeBox(runtime, glm::vec3(0.0f, 5.0f, 0.0f), rb::BodyType::Dynamic, false);
    physics.onPlayBegin(runtime);

    rb::PhysicsCommands& commands = runtime.resource<rb::PhysicsCommands>();
    commands.queue.push_back(
        {box, rb::PhysicsCommands::Op::SetVelocity, glm::vec3(5.0f, 0.0f, 0.0f)});
    for (int i = 0; i < 60; ++i) {
        physics.onUpdate(runtime, kStep);
    }
    const float x = runtime.scene().get<rb::Transform>(box).position.x;
    CHECK(x > 4.0f); // ~5 m/s for ~1 s (minus Jolt's default damping)
    CHECK(x < 6.0f);
    CHECK(commands.queue.empty()); // drained by the first update

    const rb::PhysicsState& state = runtime.resource<rb::PhysicsState>();
    const auto it = state.linearVelocity.find(box);
    CHECK(it != state.linearVelocity.end());
    if (it != state.linearVelocity.end()) {
        CHECK(it->second.x > 4.0f);
    }
    physics.onPlayEnd(runtime);
    CHECK(runtime.resource<rb::PhysicsState>().linearVelocity.empty());
}

// An impulse on a 1 kg floating body reads back as velocity of the impulse magnitude.
void impulseAcceleratesBody() {
    rb::Runtime runtime;
    rb::PhysicsSystem physics;
    const rb::Entity box = makeBox(runtime, glm::vec3(0.0f), rb::BodyType::Dynamic, false);
    physics.onPlayBegin(runtime);
    runtime.resource<rb::PhysicsCommands>().queue.push_back(
        {box, rb::PhysicsCommands::Op::Impulse, glm::vec3(0.0f, 0.0f, 3.0f)});
    for (int i = 0; i < 30; ++i) {
        physics.onUpdate(runtime, kStep);
    }
    const float z = runtime.scene().get<rb::Transform>(box).position.z;
    CHECK(z > 1.0f); // ~3 m/s for ~0.5 s
    CHECK(z < 2.0f);
}

// End to end through the real scheduler, ordered as the editor registers it (ScriptSystem
// before PhysicsSystem): a script's set_velocity moves its own rigidbody.
void scriptDrivesPhysicsBody() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = rb::Uuid::generate();
    rb::ScriptAsset asset;
    asset.source = "function on_update(self, dt) self:set_velocity(3.0, 0.0, 0.0) end\n";
    assets.add<rb::ScriptAsset>(std::move(asset), id);

    const rb::Entity ball =
        makeBox(runtime, glm::vec3(0.0f, 5.0f, 0.0f), rb::BodyType::Dynamic, false);
    rb::ScriptComponent component;
    component.script = id;
    component.handle = assets.find<rb::ScriptAsset>(id);
    runtime.scene().add<rb::ScriptComponent>(ball, component);

    runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    runtime.addSystem<rb::PhysicsSystem, rb::SystemPhase::Play>();
    runtime.start();
    runtime.beginPlay();
    runtime.setPlaying(true);
    for (int i = 0; i < 60; ++i) {
        runtime.tick(kStep);
    }
    const float x = runtime.scene().get<rb::Transform>(ball).position.x;
    CHECK(x > 2.0f); // ~3 m/s held for ~1 s
    CHECK(x < 4.0f);
    runtime.setPlaying(false);
    runtime.endPlay();
}

} // namespace

int main() {
    dynamicFallsAndLands();
    staticStaysPut();
    noGravityFloats();
    rebuildsBetweenSessions();
    accumulatorGatesAndCaps();
    velocityCommandDrivesBodyAndPublishes();
    impulseAcceleratesBody();
    scriptDrivesPhysicsBody();
    return rbtest::summary("physics");
}

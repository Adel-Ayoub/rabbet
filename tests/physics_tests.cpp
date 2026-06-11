#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/PhysicsSystem.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/scene/Transform.h"
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

} // namespace

int main() {
    dynamicFallsAndLands();
    staticStaysPut();
    noGravityFloats();
    rebuildsBetweenSessions();
    return rbtest::summary("physics");
}

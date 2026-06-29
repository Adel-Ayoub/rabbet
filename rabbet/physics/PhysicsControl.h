#pragma once

#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "rabbet/ecs/Entity.h"

namespace rb {

// Gameplay -> physics bridge. Scripts (and anything else that runs before PhysicsSystem
// in the tick) queue commands here; PhysicsSystem drains the queue into the Jolt bodies
// at the start of its update. Components stay pure data and the Jolt handles never leave
// the system. Commands against entities with no simulated body are dropped.
struct PhysicsCommands {
    enum class Op { SetVelocity, Impulse };

    struct Command {
        Entity entity;
        Op op = Op::SetVelocity;
        glm::vec3 value{0.0f};
    };

    std::vector<Command> queue;
};

// Physics -> gameplay readback, published by PhysicsSystem after each stepped frame: the
// linear velocity of every simulated non-static body, keyed by entity. A read is one step
// stale by construction (scripts run before the step that follows their commands).
struct PhysicsState {
    std::unordered_map<Entity, glm::vec3> linearVelocity;
};

} // namespace rb

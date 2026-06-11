#pragma once

namespace rb {

enum class BodyType { Static, Dynamic, Kinematic };

// Makes an entity a physics body in Play mode. The shape comes from a BoxCollider or
// SphereCollider on the same entity; without one, the entity is not simulated. Runtime
// body state lives in the PhysicsSystem (keyed by entity), so this stays plain data.
struct RigidBody {
    BodyType type = BodyType::Dynamic;
    float mass = 1.0f;
    float friction = 0.2f;
    float restitution = 0.0f;
    bool gravity = true;
};

} // namespace rb

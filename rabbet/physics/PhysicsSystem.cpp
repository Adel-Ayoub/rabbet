#include "rabbet/physics/PhysicsSystem.h"

#include <algorithm>
#include <thread>
#include <unordered_map>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/physics/SphereCollider.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/util/Log.h"

namespace rb {
namespace {

// Two object layers (and matching broad-phase layers): static geometry and moving bodies.
namespace Layers {
constexpr JPH::ObjectLayer kNonMoving = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::ObjectLayer kCount = 2;
} // namespace Layers

namespace BroadPhase {
constexpr JPH::BroadPhaseLayer kNonMoving{0};
constexpr JPH::BroadPhaseLayer kMoving{1};
constexpr JPH::uint kCount = 2;
} // namespace BroadPhase

class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface {
public:
    BroadPhaseLayerMap() {
        m_map[Layers::kNonMoving] = BroadPhase::kNonMoving;
        m_map[Layers::kMoving] = BroadPhase::kMoving;
    }
    JPH::uint GetNumBroadPhaseLayers() const override { return BroadPhase::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return m_map[layer];
    }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override {
        return layer == BroadPhase::kNonMoving ? "NonMoving" : "Moving";
    }
#endif
private:
    JPH::BroadPhaseLayer m_map[Layers::kCount];
};

class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer object, JPH::BroadPhaseLayer broad) const override {
        if (object == Layers::kNonMoving) {
            return broad == BroadPhase::kMoving; // static collides only with moving
        }
        return true;
    }
};

class ObjectLayerPairFilter final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        if (a == Layers::kNonMoving) {
            return b == Layers::kMoving;
        }
        return true;
    }
};

// Jolt's process-wide setup (allocator, factory, type registration) runs exactly once,
// before any Jolt allocation. Left registered for the process lifetime.
void ensureJoltGlobals() {
    static const bool once = [] {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
        return true;
    }();
    (void)once;
}

JPH::RVec3 toJolt(const glm::vec3& v) {
    return JPH::RVec3(v.x, v.y, v.z);
}

JPH::Quat toJolt(const glm::quat& q) {
    return JPH::Quat(q.x, q.y, q.z, q.w);
}

JPH::EMotionType motionType(BodyType type) {
    switch (type) {
    case BodyType::Static:
        return JPH::EMotionType::Static;
    case BodyType::Kinematic:
        return JPH::EMotionType::Kinematic;
    case BodyType::Dynamic:
        break;
    }
    return JPH::EMotionType::Dynamic;
}

// Advance physics in fixed substeps so the simulation is frame-rate independent; cap the
// substeps per frame so a long hitch can't trigger a spiral of death (the backlog is dropped).
constexpr float kFixedTimestep = 1.0f / 60.0f;
constexpr int kMaxSubsteps = 5;

} // namespace

struct PhysicsSystem::Impl {
    struct InitGuard {
        InitGuard() { ensureJoltGlobals(); }
    };

    struct Record {
        JPH::BodyID id;
        glm::vec3 offset{0.0f};
    };

    // Declaration order matters: globals init first; the layer interfaces must outlive the
    // physics system (so they are declared before it and destroyed after it).
    InitGuard m_init;
    JPH::TempAllocatorImpl m_tempAllocator{10 * 1024 * 1024};
    JPH::JobSystemThreadPool m_jobSystem{JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
                                         std::max(1, static_cast<int>(
                                                        std::thread::hardware_concurrency()) -
                                                        1)};
    BroadPhaseLayerMap m_broadPhaseMap;
    ObjectVsBroadPhaseFilter m_objectVsBroadPhase;
    ObjectLayerPairFilter m_objectPairFilter;
    JPH::PhysicsSystem m_physics;
    std::unordered_map<Entity, Record> m_bodies;
    float m_accumulator = 0.0f;

    Impl() {
        m_physics.Init(1024, 0, 1024, 1024, m_broadPhaseMap, m_objectVsBroadPhase,
                       m_objectPairFilter);
    }

    void teardown() {
        JPH::BodyInterface& bodies = m_physics.GetBodyInterface();
        for (const auto& [entity, record] : m_bodies) {
            bodies.RemoveBody(record.id);
            bodies.DestroyBody(record.id);
        }
        m_bodies.clear();
    }

    void build(Runtime& runtime) {
        teardown();
        m_accumulator = 0.0f;
        Scene& scene = runtime.scene();
        JPH::BodyInterface& bodies = m_physics.GetBodyInterface();

        scene.each<Transform, RigidBody>([&](Entity entity, Transform& transform, RigidBody& body) {
            JPH::RefConst<JPH::Shape> shape;
            glm::vec3 offset{0.0f};

            if (const BoxCollider* box = scene.tryGet<BoxCollider>(entity)) {
                const float minExtent =
                    std::min({box->halfExtents.x, box->halfExtents.y, box->halfExtents.z});
                const float convexRadius = std::min(0.05f, std::max(minExtent * 0.5f, 0.0f));
                JPH::BoxShapeSettings settings(toJolt(box->halfExtents), convexRadius);
                const JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError()) {
                    log::error("physics: box shape error: {}", result.GetError().c_str());
                    return;
                }
                shape = result.Get();
                offset = box->offset;
            } else if (const SphereCollider* sphere = scene.tryGet<SphereCollider>(entity)) {
                JPH::SphereShapeSettings settings(std::max(sphere->radius, 0.01f));
                const JPH::ShapeSettings::ShapeResult result = settings.Create();
                if (result.HasError()) {
                    log::error("physics: sphere shape error: {}", result.GetError().c_str());
                    return;
                }
                shape = result.Get();
                offset = sphere->offset;
            } else {
                return; // a RigidBody with no collider is not simulated
            }

            const glm::vec3 worldPos = transform.position + transform.rotation * offset;
            const JPH::ObjectLayer layer =
                body.type == BodyType::Static ? Layers::kNonMoving : Layers::kMoving;
            JPH::BodyCreationSettings settings(shape, toJolt(worldPos), toJolt(transform.rotation),
                                               motionType(body.type), layer);
            settings.mFriction = body.friction;
            settings.mRestitution = body.restitution;
            settings.mGravityFactor = body.gravity ? 1.0f : 0.0f;
            if (body.type == BodyType::Dynamic) {
                settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
                settings.mMassPropertiesOverride.mMass = std::max(body.mass, 0.001f);
            }

            const JPH::EActivation activation = body.type == BodyType::Static
                                                    ? JPH::EActivation::DontActivate
                                                    : JPH::EActivation::Activate;
            const JPH::BodyID id = bodies.CreateAndAddBody(settings, activation);
            if (!id.IsInvalid()) {
                m_bodies.emplace(entity, Record{id, offset});
            } else {
                log::warn("physics: could not create a body for entity {} (body limit reached?)",
                          entity.index());
            }
        });

        m_physics.OptimizeBroadPhase();
    }

    void step(Runtime& runtime, float dt) {
        if (m_bodies.empty()) {
            return;
        }
        m_accumulator += std::max(dt, 0.0f);
        int steps = 0;
        while (m_accumulator >= kFixedTimestep && steps < kMaxSubsteps) {
            m_physics.Update(kFixedTimestep, 1, &m_tempAllocator, &m_jobSystem);
            m_accumulator -= kFixedTimestep;
            ++steps;
        }
        if (m_accumulator > kFixedTimestep) {
            m_accumulator = 0.0f; // dropped backlog after a hitch (hit the substep cap)
        }
        if (steps == 0) {
            return; // not enough elapsed time to advance a fixed step this frame
        }

        Scene& scene = runtime.scene();
        JPH::BodyInterface& bodies = m_physics.GetBodyInterface();
        for (const auto& [entity, record] : m_bodies) {
            if (!scene.alive(entity)) {
                continue;
            }
            const RigidBody* body = scene.tryGet<RigidBody>(entity);
            Transform* transform = scene.tryGet<Transform>(entity);
            if (body == nullptr || transform == nullptr || body->type == BodyType::Static) {
                continue;
            }
            const JPH::RVec3 position = bodies.GetPosition(record.id);
            const JPH::Quat rotation = bodies.GetRotation(record.id);
            const glm::quat q(rotation.GetW(), rotation.GetX(), rotation.GetY(), rotation.GetZ());
            const glm::vec3 center(position.GetX(), position.GetY(), position.GetZ());
            transform->rotation = q;
            transform->position = center - q * record.offset; // undo the collider offset
        }
    }
};

PhysicsSystem::PhysicsSystem() : m_impl(std::make_unique<Impl>()) {}
PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::onUpdate(Runtime& runtime, float dt) { m_impl->step(runtime, dt); }
void PhysicsSystem::onPlayBegin(Runtime& runtime) { m_impl->build(runtime); }
void PhysicsSystem::onPlayEnd(Runtime&) { m_impl->teardown(); }

} // namespace rb

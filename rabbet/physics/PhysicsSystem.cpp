#include "rabbet/physics/PhysicsSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/EActivation.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/PhysicsControl.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/physics/SphereCollider.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/terrain/TerrainRenderData.h"
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
        bool terrain = false;
        std::uint32_t terrainRevision = 0; // rebuilt terrain meshes get a fresh body
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
    // One warning per entity per session for creation failures and commands aimed at
    // entities that never became bodies; reconcile runs every tick, so per-occurrence
    // logging would flood.
    std::unordered_set<Entity> m_warned;
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
        m_warned.clear();
    }

    void build(Runtime& runtime) {
        teardown();
        m_accumulator = 0.0f;
        Scene& scene = runtime.scene();
        JPH::BodyInterface& bodies = m_physics.GetBodyInterface();

        scene.each<Transform, RigidBody>([&](Entity entity, Transform&, RigidBody& body) {
            createBody(scene, bodies, entity, body);
        });
        buildTerrainBodies(runtime, bodies);
        m_physics.OptimizeBroadPhase();
    }

    void createBody(Scene& scene, JPH::BodyInterface& bodies, Entity entity,
                    const RigidBody& body) {
        JPH::RefConst<JPH::Shape> shape;
        glm::vec3 offset{0.0f};

        if (const BoxCollider* box = scene.tryGet<BoxCollider>(entity)) {
            const float minExtent =
                std::min({box->halfExtents.x, box->halfExtents.y, box->halfExtents.z});
            const float convexRadius = std::min(0.05f, std::max(minExtent * 0.5f, 0.0f));
            JPH::BoxShapeSettings settings(toJolt(box->halfExtents), convexRadius);
            const JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError()) {
                if (m_warned.insert(entity).second) {
                    log::error("physics: box shape error: {}", result.GetError().c_str());
                }
                return;
            }
            shape = result.Get();
            offset = box->offset;
        } else if (const SphereCollider* sphere = scene.tryGet<SphereCollider>(entity)) {
            JPH::SphereShapeSettings settings(std::max(sphere->radius, 0.01f));
            const JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError()) {
                if (m_warned.insert(entity).second) {
                    log::error("physics: sphere shape error: {}", result.GetError().c_str());
                }
                return;
            }
            shape = result.Get();
            offset = sphere->offset;
        } else {
            return; // a RigidBody with no collider is not simulated
        }

        // Bodies live in world space; a parented entity's Transform is local, so
        // compose the chain for the spawn pose (a root's pose is its own Transform).
        const WorldPose pose = worldPoseOf(scene, entity);
        const glm::vec3 worldPos = pose.position + pose.rotation * offset;
        const JPH::ObjectLayer layer =
            body.type == BodyType::Static ? Layers::kNonMoving : Layers::kMoving;
        JPH::BodyCreationSettings settings(shape, toJolt(worldPos), toJolt(pose.rotation),
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
        } else if (m_warned.insert(entity).second) {
            log::warn("physics: could not create a body for entity {} (body limit reached?)",
                      entity.index());
        }
    }

    // Every published terrain becomes a static mesh collider built from the exact geometry
    // the renderer draws (TerrainSystem owns the MeshData and keeps the pointer stable), so
    // collision matches the visuals with no second heightfield sampling pass. Terrains need
    // no RigidBody/collider components; the ground is solid by virtue of existing.
    void buildTerrainBodies(Runtime& runtime, JPH::BodyInterface& bodies) {
        const TerrainRenderData* terrain = runtime.tryResource<TerrainRenderData>();
        if (terrain == nullptr) {
            return;
        }
        Scene& scene = runtime.scene();
        for (const TerrainDraw& draw : terrain->draws) {
            createTerrainBody(scene, bodies, draw);
        }
    }

    void createTerrainBody(Scene& scene, JPH::BodyInterface& bodies, const TerrainDraw& draw) {
        if (draw.mesh == nullptr || draw.mesh->indices.size() < 3 || !scene.alive(draw.entity)) {
            return;
        }
        // An entity carrying both a RigidBody+collider and a TerrainComponent already owns
        // a body record; a second emplace would silently fail and leak the Jolt body.
        if (m_bodies.count(draw.entity) != 0) {
            if (m_warned.insert(draw.entity).second) {
                log::warn("physics: entity {} has both a rigid body and a terrain; the "
                          "terrain is not simulated",
                          draw.entity.index());
            }
            return;
        }
        const Transform* transform = scene.tryGet<Transform>(draw.entity);
        if (transform == nullptr) {
            return;
        }
        JPH::VertexList vertices;
        vertices.reserve(draw.mesh->vertices.size());
        for (const Vertex& vertex : draw.mesh->vertices) {
            vertices.push_back(
                JPH::Float3(vertex.position.x, vertex.position.y, vertex.position.z));
        }
        JPH::IndexedTriangleList triangles;
        triangles.reserve(draw.mesh->indices.size() / 3);
        for (std::size_t i = 0; i + 2 < draw.mesh->indices.size(); i += 3) {
            triangles.push_back(JPH::IndexedTriangle(draw.mesh->indices[i],
                                                     draw.mesh->indices[i + 1],
                                                     draw.mesh->indices[i + 2]));
        }
        JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
        const JPH::ShapeSettings::ShapeResult result = settings.Create();
        if (result.HasError()) {
            if (m_warned.insert(draw.entity).second) {
                log::error("physics: terrain shape error: {}", result.GetError().c_str());
            }
            return;
        }
        JPH::BodyCreationSettings body(result.Get(), toJolt(transform->position),
                                       toJolt(transform->rotation), JPH::EMotionType::Static,
                                       Layers::kNonMoving);
        body.mFriction = 0.8f; // grippy default so rolling bodies do not glide forever
        const JPH::BodyID id = bodies.CreateAndAddBody(body, JPH::EActivation::DontActivate);
        if (!id.IsInvalid()) {
            Record record{id, glm::vec3(0.0f)};
            record.terrain = true;
            record.terrainRevision = draw.revision;
            m_bodies.emplace(draw.entity, record);
        } else if (m_warned.insert(draw.entity).second) {
            log::warn("physics: could not create a terrain body for entity {}",
                      draw.entity.index());
        }
    }

    // The scene changes under a running session: world.spawn adds bodies, destroys (script
    // or editor) remove them, and a terrain rebuild swaps its mesh. Reconciling every tick
    // keeps the Jolt world mirroring the live scene instead of the play-edge snapshot;
    // without it destroyed entities kept colliding as invisible ghosts and spawned ones
    // never simulated at all.
    void reconcile(Runtime& runtime) {
        Scene& scene = runtime.scene();
        JPH::BodyInterface& bodies = m_physics.GetBodyInterface();
        const TerrainRenderData* terrain = runtime.tryResource<TerrainRenderData>();

        for (auto it = m_bodies.begin(); it != m_bodies.end();) {
            bool keep = scene.alive(it->first);
            if (keep && it->second.terrain) {
                keep = false;
                if (terrain != nullptr) {
                    for (const TerrainDraw& draw : terrain->draws) {
                        if (draw.entity == it->first && draw.mesh != nullptr &&
                            draw.revision == it->second.terrainRevision) {
                            keep = true;
                            break;
                        }
                    }
                }
            } else if (keep) {
                keep = scene.tryGet<RigidBody>(it->first) != nullptr;
            }
            if (keep) {
                ++it;
            } else {
                bodies.RemoveBody(it->second.id);
                bodies.DestroyBody(it->second.id);
                it = m_bodies.erase(it);
            }
        }

        scene.each<Transform, RigidBody>([&](Entity entity, Transform&, RigidBody& body) {
            if (m_bodies.count(entity) == 0 && m_warned.count(entity) == 0) {
                createBody(scene, bodies, entity, body);
            }
        });
        if (terrain != nullptr) {
            for (const TerrainDraw& draw : terrain->draws) {
                if (m_bodies.count(draw.entity) == 0 && m_warned.count(draw.entity) == 0) {
                    createTerrainBody(scene, bodies, draw);
                }
            }
        }
    }

    // Drains queued gameplay commands into the Jolt bodies. Runs before the step so a
    // command issued by this tick's scripts takes effect in this tick's simulation.
    void applyCommands(Runtime& runtime) {
        PhysicsCommands* commands = runtime.tryResource<PhysicsCommands>();
        if (commands == nullptr || commands->queue.empty()) {
            return;
        }
        Scene& scene = runtime.scene();
        JPH::BodyInterface& bodies = m_physics.GetBodyInterface();
        for (const PhysicsCommands::Command& command : commands->queue) {
            const auto it = m_bodies.find(command.entity);
            if (it == m_bodies.end()) {
                if (scene.alive(command.entity) && m_warned.insert(command.entity).second) {
                    log::warn("physics: entity {} got a command but has no simulated body "
                              "(missing collider?)",
                              command.entity.index());
                }
                continue;
            }
            const RigidBody* body = scene.tryGet<RigidBody>(command.entity);
            if (body == nullptr || body->type == BodyType::Static) {
                continue; // static bodies do not move; Jolt would reject the write
            }
            switch (command.op) {
            case PhysicsCommands::Op::SetVelocity:
                bodies.SetLinearVelocity(it->second.id, toJolt(command.value));
                break;
            case PhysicsCommands::Op::Impulse:
                bodies.AddImpulse(it->second.id, toJolt(command.value));
                break;
            }
            bodies.ActivateBody(it->second.id); // a sleeping body must wake to take the push
        }
        commands->queue.clear();
    }

    // Kinematic bodies are driven by their Transforms (scripts and the editor move those),
    // so push the composed pose into Jolt before stepping; MoveKinematic derives the
    // velocities that carry dynamic bodies standing on them.
    void syncKinematicBodies(Scene& scene, JPH::BodyInterface& bodies, float duration) {
        for (const auto& [entity, record] : m_bodies) {
            if (record.terrain || !scene.alive(entity)) {
                continue;
            }
            const RigidBody* body = scene.tryGet<RigidBody>(entity);
            if (body == nullptr || body->type != BodyType::Kinematic) {
                continue;
            }
            const WorldPose pose = worldPoseOf(scene, entity);
            const glm::vec3 target = pose.position + pose.rotation * record.offset;
            const JPH::RVec3 current = bodies.GetPosition(record.id);
            const JPH::Quat currentRot = bodies.GetRotation(record.id);
            const glm::vec3 delta = target - glm::vec3(current.GetX(), current.GetY(),
                                                       current.GetZ());
            const float rotDot = std::fabs(currentRot.GetW() * pose.rotation.w +
                                           currentRot.GetX() * pose.rotation.x +
                                           currentRot.GetY() * pose.rotation.y +
                                           currentRot.GetZ() * pose.rotation.z);
            if (glm::dot(delta, delta) < 1.0e-10f && rotDot > 1.0f - 1.0e-6f) {
                continue; // already there; leave a resting body asleep
            }
            bodies.ActivateBody(record.id);
            bodies.MoveKinematic(record.id, toJolt(target), toJolt(pose.rotation), duration);
        }
    }

    void step(Runtime& runtime, float dt) {
        reconcile(runtime); // before the drain, so a spawn-tick command hits a live body
        applyCommands(runtime);
        if (m_bodies.empty()) {
            return;
        }
        Scene& scene = runtime.scene();
        m_accumulator += std::max(dt, 0.0f);
        const int steps =
            std::min(static_cast<int>(m_accumulator / kFixedTimestep), kMaxSubsteps);
        if (steps == 0) {
            return; // not enough elapsed time to advance a fixed step this frame
        }
        syncKinematicBodies(scene, m_physics.GetBodyInterface(),
                            static_cast<float>(steps) * kFixedTimestep);
        for (int i = 0; i < steps; ++i) {
            m_physics.Update(kFixedTimestep, 1, &m_tempAllocator, &m_jobSystem);
            m_accumulator -= kFixedTimestep;
        }
        if (m_accumulator > kFixedTimestep) {
            m_accumulator = 0.0f; // dropped backlog after a hitch (hit the substep cap)
        }

        if (!runtime.hasResource<PhysicsState>()) {
            runtime.addResource<PhysicsState>();
        }
        PhysicsState& state = runtime.resource<PhysicsState>();
        state.linearVelocity.clear();

        JPH::BodyInterface& bodies = m_physics.GetBodyInterface();

        // Snapshot the poses, then apply parents before children: a dynamic child under a
        // dynamic parent must convert against the parent's POST-step pose, and map order
        // would make that a coin toss. Kinematic bodies only publish velocity; their
        // Transforms are authored, not written back.
        struct WriteBack {
            Entity entity;
            glm::quat rotation;
            glm::vec3 worldPos;
            int depth;
        };
        std::vector<WriteBack> writes;
        writes.reserve(m_bodies.size());
        for (const auto& [entity, record] : m_bodies) {
            if (record.terrain || !scene.alive(entity)) {
                continue;
            }
            const RigidBody* body = scene.tryGet<RigidBody>(entity);
            Transform* transform = scene.tryGet<Transform>(entity);
            if (body == nullptr || transform == nullptr || body->type == BodyType::Static) {
                continue;
            }
            const JPH::Vec3 velocity = bodies.GetLinearVelocity(record.id);
            state.linearVelocity[entity] =
                glm::vec3(velocity.GetX(), velocity.GetY(), velocity.GetZ());
            if (body->type != BodyType::Dynamic) {
                continue;
            }
            const JPH::RVec3 position = bodies.GetPosition(record.id);
            const JPH::Quat rotation = bodies.GetRotation(record.id);
            const glm::quat q(rotation.GetW(), rotation.GetX(), rotation.GetY(),
                              rotation.GetZ());
            const glm::vec3 center(position.GetX(), position.GetY(), position.GetZ());
            int depth = 0;
            for (Entity walk = parentOf(scene, entity);
                 walk.valid() && depth < kMaxHierarchyDepth; walk = parentOf(scene, walk)) {
                ++depth;
            }
            writes.push_back({entity, q, center - q * record.offset, depth});
        }
        std::sort(writes.begin(), writes.end(), [](const WriteBack& a, const WriteBack& b) {
            return a.depth != b.depth ? a.depth < b.depth : a.entity.index() < b.entity.index();
        });

        for (const WriteBack& write : writes) {
            Transform* transform = scene.tryGet<Transform>(write.entity);
            const Entity parent = parentOf(scene, write.entity);
            if (!parent.valid()) {
                transform->rotation = write.rotation;
                transform->position = write.worldPos;
            } else {
                // The body simulated in world space; the Transform is local to the
                // parent. This system runs before TransformSystem, so live Transforms
                // are this tick's truth (ancestor bodies were applied above first).
                const WorldPose up = worldPoseOf(scene, parent);
                const glm::quat inverse = glm::inverse(up.rotation);
                const auto safe = [](float s) { return std::fabs(s) > 1.0e-6f ? s : 1.0f; };
                const glm::vec3 offsetLocal = inverse * (write.worldPos - up.position);
                transform->rotation = glm::normalize(inverse * write.rotation);
                transform->position = glm::vec3(offsetLocal.x / safe(up.scale.x),
                                                offsetLocal.y / safe(up.scale.y),
                                                offsetLocal.z / safe(up.scale.z));
            }
        }
    }
};

PhysicsSystem::PhysicsSystem() : m_impl(std::make_unique<Impl>()) {}
PhysicsSystem::~PhysicsSystem() = default;

void PhysicsSystem::onUpdate(Runtime& runtime, float dt) { m_impl->step(runtime, dt); }

void PhysicsSystem::onPlayBegin(Runtime& runtime) {
    // The bridge resources exist from the first script tick of the session.
    if (!runtime.hasResource<PhysicsCommands>()) {
        runtime.addResource<PhysicsCommands>();
    }
    if (!runtime.hasResource<PhysicsState>()) {
        runtime.addResource<PhysicsState>();
    }
    m_impl->build(runtime);
}

void PhysicsSystem::onPlayEnd(Runtime& runtime) {
    m_impl->teardown();
    if (PhysicsCommands* commands = runtime.tryResource<PhysicsCommands>()) {
        commands->queue.clear();
    }
    if (PhysicsState* state = runtime.tryResource<PhysicsState>()) {
        state->linearVelocity.clear();
    }
}

} // namespace rb

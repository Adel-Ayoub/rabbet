#pragma once

#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "rabbet/ecs/Entity.h"

namespace rb {

class Scene;

// Walk guard: a parent chain deeper than this is treated as corrupt (a cycle authored
// around setParent's gate, e.g. by hand-editing a scene file) and the walk stops as if
// it had reached a root.
inline constexpr int kMaxHierarchyDepth = 256;

// The live parent, or kNullEntity when unparented or the parent was destroyed. Versioned
// handles make a stale link read as dead, so orphans behave as roots everywhere.
[[nodiscard]] Entity parentOf(Scene& scene, Entity entity);

[[nodiscard]] std::vector<Entity> childrenOf(Scene& scene, Entity entity);

[[nodiscard]] bool isAncestor(Scene& scene, Entity ancestor, Entity entity);

// Links child under parent; kNullEntity un-parents. Refuses a dead child or parent,
// self, and any parent inside child's own subtree (the cycle gate). Returns whether the
// request was applied.
bool setParent(Scene& scene, Entity child, Entity parent);

// Destroys the entity and every descendant.
void destroySubtree(Scene& scene, Entity root);

// setParent that preserves the child's world pose by rewriting its local TRS against the
// new parent's world matrix (shear from a rotated non-uniform ancestor scale collapses
// through the decompose, the TRS simplification). Returns whether the link changed.
bool setParentKeepingWorldPose(Scene& scene, Entity child, Entity parent);

struct WorldPose {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

// The entity's Transform composed up the parent chain: an ancestor's scale stretches the
// offsets beneath it, the rotation stays unit-length, and scale accumulates componentwise
// (shear from rotated non-uniform scale is deliberately dropped, the TRS simplification).
// An entity or ancestor without a Transform contributes identity, so for a root this is
// exactly its own Transform.
[[nodiscard]] WorldPose worldPoseOf(Scene& scene, Entity entity);

// The composed world matrix (ancestor locals folded down onto the entity's own), for
// one-off edit-time queries like the gizmo and re-parenting; per-tick consumers read the
// WorldMatrix component TransformSystem publishes instead.
[[nodiscard]] glm::mat4 worldMatrixOf(Scene& scene, Entity entity);

} // namespace rb

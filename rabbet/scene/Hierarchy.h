#pragma once

#include <vector>

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

} // namespace rb

#include "rabbet/scene/TransformSystem.h"

#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/WorldMatrix.h"
#include "rabbet/util/Log.h"

#include <vector>

namespace rb {

void TransformSystem::onUpdate(Runtime& runtime, float) {
    Scene& scene = runtime.scene();
    m_worlds.clear();
    scene.each<Transform>([&](Entity entity, Transform& transform) {
        // Roots take the local matrix directly, so a scene with no parent links composes
        // bit-identically to the pre-hierarchy path (and skips the memo entirely).
        const Entity parent = parentOf(scene, entity);
        const glm::mat4 world = parent.valid()
                                    ? worldOf(scene, parent, 0) * transform.matrix()
                                    : transform.matrix();
        scene.add<WorldMatrix>(entity, WorldMatrix{world});
    });

    // A removed Transform must take its WorldMatrix along, or the render and pick passes keep
    // seeing the ghost at its last pose. Collected first: removal mid-each would mutate the
    // iterated pool.
    std::vector<Entity> stale;
    scene.each<WorldMatrix>([&](Entity entity, WorldMatrix&) {
        if (!scene.has<Transform>(entity)) {
            stale.push_back(entity);
        }
    });
    for (const Entity entity : stale) {
        scene.remove<WorldMatrix>(entity);
    }
}

// The ancestor's world matrix, memoized per tick so a shared parent is composed once.
// Iteration order never matters: whichever child reaches the ancestor first fills the
// memo. An ancestor without a Transform contributes identity, which is the same as
// skipping it. A chain deeper than the guard is treated as corrupt (a cycle authored
// around setParent's gate) and cut at the cap.
glm::mat4 TransformSystem::worldOf(Scene& scene, Entity entity, int depth) {
    const auto memo = m_worlds.find(entity);
    if (memo != m_worlds.end()) {
        return memo->second;
    }
    const Transform* transform = scene.tryGet<Transform>(entity);
    const glm::mat4 local = transform != nullptr ? transform->matrix() : glm::mat4(1.0f);
    const Entity parent = parentOf(scene, entity);
    glm::mat4 world = local;
    if (parent.valid()) {
        if (depth >= kMaxHierarchyDepth) {
            if (!m_cycleWarned) {
                log::warn("transform: parent chain deeper than {}; treating entity {} as a root",
                          kMaxHierarchyDepth, entity.index());
                m_cycleWarned = true;
            }
        } else {
            world = worldOf(scene, parent, depth + 1) * local;
        }
    }
    m_worlds.emplace(entity, world);
    return world;
}

} // namespace rb

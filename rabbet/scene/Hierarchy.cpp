#include "rabbet/scene/Hierarchy.h"

#include <cmath>
#include <cstddef>
#include <unordered_set>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>

#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Parent.h"
#include "rabbet/scene/Transform.h"

namespace rb {

Entity parentOf(Scene& scene, Entity entity) {
    const Parent* link = scene.tryGet<Parent>(entity);
    if (link == nullptr || !scene.alive(link->entity)) {
        return kNullEntity;
    }
    return link->entity;
}

std::vector<Entity> childrenOf(Scene& scene, Entity entity) {
    std::vector<Entity> children;
    if (!scene.alive(entity)) {
        return children;
    }
    scene.each<Parent>([&](Entity child, Parent& link) {
        if (link.entity == entity) {
            children.push_back(child);
        }
    });
    return children;
}

bool isAncestor(Scene& scene, Entity ancestor, Entity entity) {
    if (!scene.alive(ancestor) || !scene.alive(entity)) {
        return false;
    }
    // Full walk, no depth cap: capping here once let a >cap chain close a cycle through
    // setParent's gate. The slow pointer advances half as fast; meeting it means the walk
    // entered a cycle that never reached `ancestor`.
    Entity slow = parentOf(scene, entity);
    Entity fast = slow;
    while (fast.valid()) {
        if (fast == ancestor) {
            return true;
        }
        fast = parentOf(scene, fast);
        if (!fast.valid()) {
            return false;
        }
        if (fast == ancestor) {
            return true;
        }
        fast = parentOf(scene, fast);
        slow = parentOf(scene, slow);
        if (fast.valid() && fast == slow) {
            return false;
        }
    }
    return false;
}

std::vector<Entity> collectSubtree(Scene& scene, Entity root) {
    std::vector<Entity> members;
    if (!scene.alive(root)) {
        return members;
    }
    // The seen set makes the walk terminate even over corrupt cyclic links; without it a
    // cycle re-enqueues its members forever.
    std::unordered_set<Entity> seen{root};
    members.push_back(root);
    for (std::size_t next = 0; next < members.size(); ++next) {
        for (const Entity child : childrenOf(scene, members[next])) {
            if (seen.insert(child).second) {
                members.push_back(child);
            }
        }
    }
    return members;
}

bool setParent(Scene& scene, Entity child, Entity parent) {
    if (!scene.alive(child)) {
        return false;
    }
    if (!parent.valid()) {
        scene.remove<Parent>(child);
        return true;
    }
    // Self or anything below child would loop the chain; refuse instead of relinking.
    if (!scene.alive(parent) || parent == child || isAncestor(scene, child, parent)) {
        return false;
    }
    scene.add<Parent>(child, Parent{parent});
    return true;
}

namespace {

bool isFinite(const glm::mat4& m) {
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            if (!std::isfinite(m[c][r])) {
                return false;
            }
        }
    }
    return true;
}

bool isFinite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool isFinite(const glm::quat& q) {
    return std::isfinite(q.w) && std::isfinite(q.x) && std::isfinite(q.y) &&
           std::isfinite(q.z);
}

} // namespace

bool setParentKeepingWorldPose(Scene& scene, Entity child, Entity parent) {
    const Entity previous = parentOf(scene, child);
    const glm::mat4 world = worldMatrixOf(scene, child);
    if (!setParent(scene, child, parent)) {
        return false;
    }
    Transform* transform = scene.tryGet<Transform>(child);
    if (transform == nullptr) {
        return true; // linked; there is no local TRS to rewrite
    }
    const Entity actual = parentOf(scene, child);
    const glm::mat4 local =
        actual.valid() ? glm::inverse(worldMatrixOf(scene, actual)) * world : world;
    glm::vec3 translation;
    glm::vec3 scale;
    glm::vec3 skew;
    glm::vec4 perspective;
    glm::quat rotation;
    // A degenerate ancestor scale makes the inverse blow up (glm::decompose passes NaN
    // through its guards and would "succeed"), and a zero-scale pose can decompose into
    // non-finite parts. Writing that local TRS corrupts the Transform and, since NaN
    // serializes as JSON null, loses the component on the next load. Refuse the gesture
    // and restore the old link instead.
    if (!isFinite(local) ||
        !glm::decompose(local, scale, rotation, translation, skew, perspective) ||
        !isFinite(translation) || !isFinite(rotation) || !isFinite(scale)) {
        setParent(scene, child, previous);
        return false;
    }
    transform->position = translation;
    transform->rotation = rotation;
    transform->scale = scale;
    return true;
}

WorldPose worldPoseOf(Scene& scene, Entity entity) {
    WorldPose pose;
    if (const Transform* transform = scene.tryGet<Transform>(entity)) {
        pose.position = transform->position;
        pose.rotation = transform->rotation;
        pose.scale = transform->scale;
    }
    Entity walk = parentOf(scene, entity);
    for (int depth = 0; walk.valid() && depth < kMaxHierarchyDepth; ++depth) {
        if (const Transform* up = scene.tryGet<Transform>(walk)) {
            pose.position = up->position + up->rotation * (up->scale * pose.position);
            pose.rotation = glm::normalize(up->rotation * pose.rotation);
            pose.scale = up->scale * pose.scale;
        }
        walk = parentOf(scene, walk);
    }
    return pose;
}

glm::mat4 worldMatrixOf(Scene& scene, Entity entity) {
    const Transform* transform = scene.tryGet<Transform>(entity);
    glm::mat4 world = transform != nullptr ? transform->matrix() : glm::mat4(1.0f);
    Entity walk = parentOf(scene, entity);
    for (int depth = 0; walk.valid() && depth < kMaxHierarchyDepth; ++depth) {
        if (const Transform* up = scene.tryGet<Transform>(walk)) {
            world = up->matrix() * world;
        }
        walk = parentOf(scene, walk);
    }
    return world;
}

void destroySubtree(Scene& scene, Entity root) {
    // Collect first: destroying while childrenOf scans the Parent pool would mutate the
    // pool mid-iteration.
    for (const Entity entity : collectSubtree(scene, root)) {
        scene.destroy(entity);
    }
}

} // namespace rb

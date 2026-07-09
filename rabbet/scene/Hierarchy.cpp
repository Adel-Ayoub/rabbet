#include "rabbet/scene/Hierarchy.h"

#include <cstddef>

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
    Entity walk = parentOf(scene, entity);
    for (int depth = 0; walk.valid() && depth < kMaxHierarchyDepth; ++depth) {
        if (walk == ancestor) {
            return true;
        }
        walk = parentOf(scene, walk);
    }
    return false;
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

bool setParentKeepingWorldPose(Scene& scene, Entity child, Entity parent) {
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
    if (glm::decompose(local, scale, rotation, translation, skew, perspective)) {
        transform->position = translation;
        transform->rotation = rotation;
        transform->scale = scale;
    }
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
    if (!scene.alive(root)) {
        return;
    }
    // Collect first: destroying while childrenOf scans the Parent pool would mutate the
    // pool mid-iteration.
    std::vector<Entity> doomed{root};
    for (std::size_t next = 0; next < doomed.size(); ++next) {
        for (const Entity child : childrenOf(scene, doomed[next])) {
            doomed.push_back(child);
        }
    }
    for (const Entity entity : doomed) {
        scene.destroy(entity);
    }
}

} // namespace rb

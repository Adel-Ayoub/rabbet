#include "rabbet/scene/Hierarchy.h"

#include <cstddef>

#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Parent.h"

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

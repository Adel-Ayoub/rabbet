#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Parent.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scene/TransformSystem.h"
#include "rabbet/scene/WorldMatrix.h"
#include "tests/Test.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <vector>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

bool approxVec(const glm::vec3& a, const glm::vec3& b) {
    return approx(a.x, b.x) && approx(a.y, b.y) && approx(a.z, b.z);
}

glm::vec3 worldOrigin(rb::Scene& scene, rb::Entity e) {
    return glm::vec3(scene.get<rb::WorldMatrix>(e).value * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
}

rb::Entity spawnAt(rb::Scene& scene, const glm::vec3& position) {
    const rb::Entity e = scene.create();
    rb::Transform t;
    t.position = position;
    scene.add<rb::Transform>(e, t);
    return e;
}

} // namespace

static void unparentedEntityReadsAsRoot() {
    rb::Scene scene;
    const rb::Entity e = scene.create();
    CHECK(rb::parentOf(scene, e) == rb::kNullEntity);
    CHECK(rb::childrenOf(scene, e).empty());
    CHECK(!rb::isAncestor(scene, e, e));
}

static void setParentLinksBothDirections() {
    rb::Scene scene;
    const rb::Entity parent = scene.create();
    const rb::Entity child = scene.create();

    CHECK(rb::setParent(scene, child, parent));
    CHECK(rb::parentOf(scene, child) == parent);

    const std::vector<rb::Entity> children = rb::childrenOf(scene, parent);
    CHECK(children.size() == 1u);
    CHECK(!children.empty() && children.front() == child);

    CHECK(rb::isAncestor(scene, parent, child));
    CHECK(!rb::isAncestor(scene, child, parent));
}

static void setParentRefusesSelfAndDead() {
    rb::Scene scene;
    const rb::Entity e = scene.create();
    CHECK(!rb::setParent(scene, e, e));

    const rb::Entity dead = scene.create();
    scene.destroy(dead);
    CHECK(!rb::setParent(scene, e, dead));
    CHECK(!rb::setParent(scene, dead, e));
    CHECK(rb::parentOf(scene, e) == rb::kNullEntity);
}

static void setParentRefusesCycles() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    const rb::Entity b = scene.create();
    const rb::Entity c = scene.create();
    CHECK(rb::setParent(scene, b, a));
    CHECK(rb::setParent(scene, c, b));

    CHECK(rb::isAncestor(scene, a, c)); // grandparent across two links
    CHECK(!rb::setParent(scene, a, c)); // a under its own grandchild would cycle
    CHECK(!rb::setParent(scene, a, b));
    CHECK(rb::parentOf(scene, a) == rb::kNullEntity);
}

static void nullParentDetaches() {
    rb::Scene scene;
    const rb::Entity parent = scene.create();
    const rb::Entity child = scene.create();
    CHECK(rb::setParent(scene, child, parent));

    CHECK(rb::setParent(scene, child, rb::kNullEntity));
    CHECK(rb::parentOf(scene, child) == rb::kNullEntity);
    CHECK(rb::childrenOf(scene, parent).empty());
}

static void reparentMovesBetweenParents() {
    rb::Scene scene;
    const rb::Entity first = scene.create();
    const rb::Entity second = scene.create();
    const rb::Entity child = scene.create();

    CHECK(rb::setParent(scene, child, first));
    CHECK(rb::setParent(scene, child, second));

    CHECK(rb::parentOf(scene, child) == second);
    CHECK(rb::childrenOf(scene, first).empty());
    CHECK(rb::childrenOf(scene, second).size() == 1u);
}

static void destroyedParentOrphansChild() {
    rb::Scene scene;
    const rb::Entity parent = scene.create();
    const rb::Entity child = scene.create();
    CHECK(rb::setParent(scene, child, parent));

    scene.destroy(parent);
    CHECK(scene.alive(child));
    CHECK(rb::parentOf(scene, child) == rb::kNullEntity);

    // The freed index comes back with a bumped version; the stale link must not adopt
    // the recycled entity as the child's parent.
    const rb::Entity recycled = scene.create();
    CHECK(recycled.index() == parent.index());
    CHECK(rb::parentOf(scene, child) == rb::kNullEntity);
    CHECK(rb::childrenOf(scene, recycled).empty());
}

static void destroySubtreeCascades() {
    rb::Scene scene;
    const rb::Entity root = scene.create();
    const rb::Entity left = scene.create();
    const rb::Entity right = scene.create();
    const rb::Entity grand = scene.create();
    const rb::Entity bystander = scene.create();
    CHECK(rb::setParent(scene, left, root));
    CHECK(rb::setParent(scene, right, root));
    CHECK(rb::setParent(scene, grand, left));

    rb::destroySubtree(scene, root);

    CHECK(!scene.alive(root));
    CHECK(!scene.alive(left));
    CHECK(!scene.alive(right));
    CHECK(!scene.alive(grand));
    CHECK(scene.alive(bystander));
    CHECK(scene.aliveCount() == 1u);
}

static void destroySubtreeOfMiddleNodeSparesTheRest() {
    rb::Scene scene;
    const rb::Entity root = scene.create();
    const rb::Entity doomed = scene.create();
    const rb::Entity kept = scene.create();
    const rb::Entity grand = scene.create();
    CHECK(rb::setParent(scene, doomed, root));
    CHECK(rb::setParent(scene, kept, root));
    CHECK(rb::setParent(scene, grand, doomed));

    rb::destroySubtree(scene, doomed);

    CHECK(scene.alive(root));
    CHECK(scene.alive(kept));
    CHECK(!scene.alive(doomed));
    CHECK(!scene.alive(grand));
    const std::vector<rb::Entity> children = rb::childrenOf(scene, root);
    CHECK(children.size() == 1u);
    CHECK(!children.empty() && children.front() == kept);
}

static void corruptCycleWalkTerminates() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    const rb::Entity b = scene.create();
    const rb::Entity outsider = scene.create();
    // Bypass setParent's gate the way a hand-edited scene file could.
    scene.add<rb::Parent>(a, rb::Parent{b});
    scene.add<rb::Parent>(b, rb::Parent{a});

    CHECK(rb::isAncestor(scene, b, a));         // one hop, before the loop matters
    CHECK(!rb::isAncestor(scene, outsider, a)); // capped walk ends instead of hanging
    CHECK(rb::setParent(scene, outsider, a));   // no new cycle through the outsider
    CHECK(rb::parentOf(scene, outsider) == a);
}

static void worldMatrixComposesThroughParent() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity parent = scene.create();
    rb::Transform pt;
    pt.position = {10.0f, 0.0f, 0.0f};
    pt.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    pt.scale = glm::vec3(2.0f);
    scene.add<rb::Transform>(parent, pt);

    const rb::Entity child = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    CHECK(rb::setParent(scene, child, parent));

    rt.start();
    rt.tick(0.0f);

    // The child's local offset is scaled then rotated into the parent's frame.
    CHECK(approxVec(worldOrigin(scene, child), glm::vec3(10.0f, 2.0f, 0.0f)));
    CHECK(approxVec(worldOrigin(scene, parent), glm::vec3(10.0f, 0.0f, 0.0f)));
    rt.stop();
}

static void threeDeepChainAccumulates() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity a = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    const rb::Entity b = spawnAt(scene, {0.0f, 1.0f, 0.0f});
    const rb::Entity c = spawnAt(scene, {0.0f, 0.0f, 1.0f});
    CHECK(rb::setParent(scene, b, a));
    CHECK(rb::setParent(scene, c, b));

    rt.start();
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, c), glm::vec3(1.0f, 1.0f, 1.0f)));
    rt.stop();
}

static void rootWorldMatrixIsBitIdenticalToLocal() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    rb::Transform t;
    t.position = {0.1f, 0.2f, 0.3f};
    t.rotation = glm::angleAxis(0.7f, glm::normalize(glm::vec3(0.3f, 0.5f, 0.8f)));
    t.scale = {1.3f, 0.7f, 2.1f};
    const rb::Entity e = scene.create();
    scene.add<rb::Transform>(e, t);

    rt.start();
    rt.tick(0.0f);

    const glm::mat4 expected = t.matrix();
    const glm::mat4 got = scene.get<rb::WorldMatrix>(e).value;
    bool identical = true;
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            identical = identical && expected[col][row] == got[col][row];
        }
    }
    CHECK(identical); // the flat path must not pick up any composition arithmetic
    rt.stop();
}

static void transformlessMiddleNodeComposesThrough() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity top = spawnAt(scene, {5.0f, 0.0f, 0.0f});
    const rb::Entity mid = scene.create(); // grouping node, no Transform
    const rb::Entity leaf = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    CHECK(rb::setParent(scene, mid, top));
    CHECK(rb::setParent(scene, leaf, mid));

    rt.start();
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, leaf), glm::vec3(6.0f, 0.0f, 0.0f)));
    CHECK(!scene.has<rb::WorldMatrix>(mid)); // only Transform holders get a world matrix
    rt.stop();
}

static void orphanComposesAsRootNextTick() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity parent = spawnAt(scene, {10.0f, 0.0f, 0.0f});
    const rb::Entity child = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    CHECK(rb::setParent(scene, child, parent));

    rt.start();
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, child), glm::vec3(11.0f, 0.0f, 0.0f)));

    scene.destroy(parent);
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, child), glm::vec3(1.0f, 0.0f, 0.0f)));
    rt.stop();
}

static void reparentRecomposesNextTick() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity first = spawnAt(scene, {3.0f, 0.0f, 0.0f});
    const rb::Entity second = spawnAt(scene, {7.0f, 0.0f, 0.0f});
    const rb::Entity child = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    CHECK(rb::setParent(scene, child, first));

    rt.start();
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, child), glm::vec3(4.0f, 0.0f, 0.0f)));

    CHECK(rb::setParent(scene, child, second));
    rt.tick(0.0f);
    CHECK(approxVec(worldOrigin(scene, child), glm::vec3(8.0f, 0.0f, 0.0f)));
    rt.stop();
}

static void reparentKeepingWorldPoseHoldsThePose() {
    rb::Scene scene;

    const rb::Entity rig = scene.create();
    rb::Transform rigPose;
    rigPose.position = {10.0f, 0.0f, 0.0f};
    rigPose.rotation = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));
    rigPose.scale = glm::vec3(2.0f);
    scene.add<rb::Transform>(rig, rigPose);

    const rb::Entity other = scene.create();
    rb::Transform otherPose;
    otherPose.position = {-5.0f, 3.0f, 0.0f};
    scene.add<rb::Transform>(other, otherPose);

    const rb::Entity child = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    CHECK(rb::setParent(scene, child, rig));

    const auto origin = [&](rb::Entity e) {
        return glm::vec3(rb::worldMatrixOf(scene, e) * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    };
    const glm::vec3 before = origin(child);
    CHECK(approxVec(before, glm::vec3(10.0f, 2.0f, 0.0f)));

    // Moving between parents rewrites the local TRS so the world position holds.
    CHECK(rb::setParentKeepingWorldPose(scene, child, other));
    CHECK(rb::parentOf(scene, child) == other);
    CHECK(approxVec(origin(child), before));

    // Un-parenting bakes the world pose straight into the local Transform.
    CHECK(rb::setParentKeepingWorldPose(scene, child, rb::kNullEntity));
    CHECK(rb::parentOf(scene, child) == rb::kNullEntity);
    CHECK(approxVec(origin(child), before));
    CHECK(approxVec(scene.get<rb::Transform>(child).position, before));
}

static void corruptCycleTickTerminates() {
    rb::Runtime rt;
    rt.addSystem<rb::TransformSystem>();
    rb::Scene& scene = rt.scene();

    const rb::Entity a = spawnAt(scene, {1.0f, 0.0f, 0.0f});
    const rb::Entity b = spawnAt(scene, {2.0f, 0.0f, 0.0f});
    scene.add<rb::Parent>(a, rb::Parent{b}); // bypass the gate, as a bad file could
    scene.add<rb::Parent>(b, rb::Parent{a});

    rt.start();
    rt.tick(0.0f); // must terminate at the depth cap, not hang or overflow
    CHECK(scene.has<rb::WorldMatrix>(a));
    CHECK(scene.has<rb::WorldMatrix>(b));
    rt.stop();
}

int main() {
    unparentedEntityReadsAsRoot();
    setParentLinksBothDirections();
    setParentRefusesSelfAndDead();
    setParentRefusesCycles();
    nullParentDetaches();
    reparentMovesBetweenParents();
    destroyedParentOrphansChild();
    destroySubtreeCascades();
    destroySubtreeOfMiddleNodeSparesTheRest();
    corruptCycleWalkTerminates();
    worldMatrixComposesThroughParent();
    threeDeepChainAccumulates();
    rootWorldMatrixIsBitIdenticalToLocal();
    transformlessMiddleNodeComposesThrough();
    orphanComposesAsRootNextTick();
    reparentRecomposesNextTick();
    reparentKeepingWorldPoseHoldsThePose();
    corruptCycleTickTerminates();
    return rbtest::summary("hierarchy");
}

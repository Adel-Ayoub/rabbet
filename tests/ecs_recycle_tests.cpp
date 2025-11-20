#include "rabbet/ecs/Scene.h"
#include "tests/Test.h"

#include <vector>

namespace {

struct Tag {
    int v = 0;
};

} // namespace

static void freeIndicesReusedLifo() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    const rb::Entity b = scene.create();
    const rb::Entity c = scene.create();
    CHECK(a.index() == 0u);
    CHECK(b.index() == 1u);
    CHECK(c.index() == 2u);

    scene.destroy(a);
    scene.destroy(c);
    // Most-recently freed slot comes back first.
    const rb::Entity d = scene.create();
    const rb::Entity e = scene.create();
    CHECK(d.index() == c.index());
    CHECK(e.index() == a.index());
    CHECK(scene.alive(b));
    CHECK(scene.alive(d));
    CHECK(scene.alive(e));
    CHECK(!scene.alive(a));
    CHECK(!scene.alive(c));
}

static void versionIncrementsEachRecycle() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    CHECK(a.version() == 0u);
    scene.destroy(a);
    const rb::Entity b = scene.create();
    CHECK(b.index() == a.index());
    CHECK(b.version() == 1u);
    scene.destroy(b);
    const rb::Entity c = scene.create();
    CHECK(c.index() == a.index());
    CHECK(c.version() == 2u);

    CHECK(a != b);
    CHECK(b != c);
    CHECK(a != c);
    CHECK(scene.alive(c));
    CHECK(!scene.alive(a));
    CHECK(!scene.alive(b));
}

static void doubleDestroyIsNoOp() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    scene.destroy(a);
    scene.destroy(a); // stale handle: must not bump the version again or change the count
    CHECK(scene.aliveCount() == 0u);

    const rb::Entity b = scene.create();
    CHECK(b.index() == a.index());
    CHECK(b.version() == 1u); // proves the second destroy did nothing
}

static void destroyInvalidIsSafe() {
    rb::Scene scene;
    scene.destroy(rb::kNullEntity); // empty scene, invalid handle
    const rb::Entity a = scene.create();
    scene.destroy(rb::Entity{999u, 0u}); // index out of range
    scene.destroy(rb::Entity{a.index(), a.version() + 5u}); // right slot, wrong version
    CHECK(scene.aliveCount() == 1u);
    CHECK(scene.alive(a));
}

static void recycledSlotHasNoLeftoverComponents() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    scene.add<Tag>(a, Tag{42});
    CHECK(scene.has<Tag>(a));
    scene.destroy(a);
    CHECK(scene.count<Tag>() == 0u);

    const rb::Entity b = scene.create();
    CHECK(b.index() == a.index());
    CHECK(!scene.has<Tag>(b));
    CHECK(scene.tryGet<Tag>(b) == nullptr);
    CHECK(!scene.has<Tag>(a)); // the stale handle still sees nothing
}

static void aliveCountTracksChurn() {
    rb::Scene scene;
    std::vector<rb::Entity> es;
    for (int i = 0; i < 5; ++i) {
        es.push_back(scene.create());
    }
    CHECK(scene.aliveCount() == 5u);

    scene.destroy(es[1]);
    scene.destroy(es[3]);
    CHECK(scene.aliveCount() == 3u);

    const rb::Entity x = scene.create();
    const rb::Entity y = scene.create();
    CHECK(scene.aliveCount() == 5u);
    CHECK(x.index() == es[3].index()); // LIFO: es[3] freed last
    CHECK(y.index() == es[1].index());
    CHECK(scene.alive(x));
    CHECK(scene.alive(y));
    CHECK(!scene.alive(es[1]));
    CHECK(!scene.alive(es[3]));
}

int main() {
    freeIndicesReusedLifo();
    versionIncrementsEachRecycle();
    doubleDestroyIsNoOp();
    destroyInvalidIsSafe();
    recycledSlotHasNoLeftoverComponents();
    aliveCountTracksChurn();
    return rbtest::summary("ecs_recycle");
}

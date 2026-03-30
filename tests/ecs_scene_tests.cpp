#include "rabbet/ecs/Scene.h"
#include "tests/Test.h"

namespace {

struct Position {
    float x = 0.0f;
    float y = 0.0f;
};

struct Velocity {
    float dx = 0.0f;
    float dy = 0.0f;
};

} // namespace

static void createAndDestroy() {
    rb::Scene scene;
    CHECK(scene.aliveCount() == 0u);

    const rb::Entity e = scene.create();
    CHECK(scene.alive(e));
    CHECK(scene.aliveCount() == 1u);

    scene.destroy(e);
    CHECK(!scene.alive(e));
    CHECK(scene.aliveCount() == 0u);
}

static void recycledIndexBumpsVersion() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    scene.destroy(a);
    const rb::Entity b = scene.create();

    CHECK(a.index() == b.index());
    CHECK(a.version() != b.version());
    CHECK(!scene.alive(a));
    CHECK(scene.alive(b));
}

static void addHasGetRemove() {
    rb::Scene scene;
    const rb::Entity e = scene.create();
    CHECK(!scene.has<Position>(e));

    scene.add<Position>(e, Position{1.0f, 2.0f});
    CHECK(scene.has<Position>(e));
    CHECK(scene.get<Position>(e).x == 1.0f);

    scene.get<Position>(e).x = 5.0f;
    CHECK(scene.get<Position>(e).x == 5.0f);

    scene.remove<Position>(e);
    CHECK(!scene.has<Position>(e));
    CHECK(scene.tryGet<Position>(e) == nullptr);
}

static void staleHandleSeesNoComponents() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    scene.add<Position>(a, Position{1.0f, 1.0f});
    scene.destroy(a);
    CHECK(!scene.has<Position>(a));

    const rb::Entity b = scene.create();
    CHECK(!scene.has<Position>(b));
}

static void destroyClearsEveryPool() {
    rb::Scene scene;
    const rb::Entity e = scene.create();
    scene.add<Position>(e, Position{0.0f, 0.0f});
    scene.add<Velocity>(e, Velocity{0.0f, 0.0f});
    CHECK(scene.count<Position>() == 1u);
    CHECK(scene.count<Velocity>() == 1u);

    scene.destroy(e);
    CHECK(scene.count<Position>() == 0u);
    CHECK(scene.count<Velocity>() == 0u);
}

static void queryVisitsIntersectionOnly() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    const rb::Entity b = scene.create();
    const rb::Entity c = scene.create();
    scene.add<Position>(a, Position{1.0f, 1.0f});
    scene.add<Velocity>(a, Velocity{1.0f, 1.0f});
    scene.add<Position>(b, Position{2.0f, 2.0f});
    scene.add<Velocity>(c, Velocity{3.0f, 3.0f});

    int visited = 0;
    rb::Entity seen;
    scene.each<Position, Velocity>([&](rb::Entity e, Position& p, Velocity& v) {
        ++visited;
        seen = e;
        p.x += v.dx;
    });
    CHECK(visited == 1);
    CHECK(seen == a);
    CHECK(scene.get<Position>(a).x == 2.0f);
}

static void queryEmptyWhenComponentUnused() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    scene.add<Position>(a, Position{1.0f, 1.0f});

    int visited = 0;
    scene.each<Position, Velocity>([&](rb::Entity, Position&, Velocity&) { ++visited; });
    CHECK(visited == 0);
}

static void clearRemovesEverything() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    scene.add<Position>(a, Position{1.0f, 2.0f});
    (void)scene.create();
    CHECK(scene.aliveCount() == 2u);

    scene.clear();
    CHECK(scene.aliveCount() == 0u);
    CHECK(scene.count<Position>() == 0u);

    const rb::Entity b = scene.create(); // scene is reusable after clear
    CHECK(scene.alive(b));
}

int main() {
    createAndDestroy();
    recycledIndexBumpsVersion();
    addHasGetRemove();
    staleHandleSeesNoComponents();
    destroyClearsEveryPool();
    queryVisitsIntersectionOnly();
    queryEmptyWhenComponentUnused();
    clearRemovesEverything();
    return rbtest::summary("ecs_scene");
}

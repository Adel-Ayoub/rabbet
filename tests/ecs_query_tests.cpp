#include "rabbet/ecs/Scene.h"
#include "tests/Test.h"

#include <unordered_set>
#include <vector>

namespace {

struct A {
    int v = 0;
};
struct B {
    int v = 0;
};
struct C {
    int v = 0;
};

} // namespace

// A many-A / few-B scene exercises the smallest-pool driver: the result must be the
// intersection regardless of which pool drives or the order of the type arguments.
static void smallestPoolDrivesIntersection() {
    rb::Scene scene;
    std::vector<rb::Entity> es;
    for (int i = 0; i < 100; ++i) {
        const rb::Entity e = scene.create();
        scene.add<A>(e, A{i});
        es.push_back(e);
    }
    const std::unordered_set<rb::Entity> withB = {es[10], es[50], es[90]};
    for (const rb::Entity e : withB) {
        scene.add<B>(e, B{7});
    }
    CHECK(scene.count<A>() == 100u);
    CHECK(scene.count<B>() == 3u);

    int visited = 0;
    std::unordered_set<rb::Entity> seen;
    scene.each<A, B>([&](rb::Entity e, A&, B&) {
        ++visited;
        seen.insert(e);
    });
    CHECK(visited == 3);
    CHECK(seen == withB);

    int reversed = 0;
    std::unordered_set<rb::Entity> seenReversed;
    scene.each<B, A>([&](rb::Entity e, B&, A&) {
        ++reversed;
        seenReversed.insert(e);
    });
    CHECK(reversed == 3);
    CHECK(seenReversed == withB);
}

static void singleEachVisitsAllAndMutatesInPlace() {
    rb::Scene scene;
    for (int i = 0; i < 20; ++i) {
        scene.add<A>(scene.create(), A{1});
    }
    int count = 0;
    scene.each<A>([&](rb::Entity, A& a) {
        a.v += 10;
        ++count;
    });
    CHECK(count == 20);

    int sum = 0;
    scene.each<A>([&](rb::Entity, A& a) { sum += a.v; });
    CHECK(sum == 20 * 11);
}

// Adding a *different* component while iterating is the pattern the transform system
// relies on; the driver pool is untouched, so it is safe.
static void addOtherComponentDuringIteration() {
    rb::Scene scene;
    std::vector<rb::Entity> es;
    for (int i = 0; i < 64; ++i) {
        const rb::Entity e = scene.create();
        scene.add<A>(e, A{i});
        es.push_back(e);
    }
    scene.each<A>([&](rb::Entity e, A& a) { scene.add<B>(e, B{a.v * 2}); });
    CHECK(scene.count<B>() == 64u);

    int matched = 0;
    for (const rb::Entity e : es) {
        if (scene.has<B>(e) && scene.get<B>(e).v == scene.get<A>(e).v * 2) {
            ++matched;
        }
    }
    CHECK(matched == 64);
}

// Removing a non-driver component mid-iteration is safe: the driver's dense array is
// unchanged, and the remaining pools are re-checked per entity.
static void removeNonDriverComponentDuringIteration() {
    rb::Scene scene;
    std::vector<rb::Entity> es;
    for (int i = 0; i < 40; ++i) {
        const rb::Entity e = scene.create();
        scene.add<A>(e, A{i});
        scene.add<B>(e, B{i});
        es.push_back(e);
    }
    // each<A, B> drives on A (first of the equal-sized pools); B is safe to mutate.
    scene.each<A, B>([&](rb::Entity e, A& a, B&) {
        if (a.v % 2 == 0) {
            scene.remove<B>(e);
        }
    });
    CHECK(scene.count<A>() == 40u);
    CHECK(scene.count<B>() == 20u);

    int keptB = 0;
    for (const rb::Entity e : es) {
        CHECK(scene.has<A>(e));
        if (scene.has<B>(e)) {
            ++keptB;
        }
    }
    CHECK(keptB == 20);
}

// Structural changes to the driver are deferred: collect during iteration, destroy after.
static void deferredDestroyDuringIteration() {
    rb::Scene scene;
    for (int i = 0; i < 25; ++i) {
        scene.add<A>(scene.create(), A{i});
    }
    std::vector<rb::Entity> doomed;
    scene.each<A>([&](rb::Entity e, A& a) {
        if (a.v < 10) {
            doomed.push_back(e);
        }
    });
    CHECK(doomed.size() == 10u);
    for (const rb::Entity e : doomed) {
        scene.destroy(e);
    }
    CHECK(scene.aliveCount() == 15u);
    CHECK(scene.count<A>() == 15u);

    int remaining = 0;
    bool allHigh = true;
    scene.each<A>([&](rb::Entity, A& a) {
        if (a.v < 10) {
            allHigh = false;
        }
        ++remaining;
    });
    CHECK(remaining == 15);
    CHECK(allHigh);
}

static void threeComponentQueryVisitsTripleIntersection() {
    rb::Scene scene;
    const rb::Entity abc = scene.create();
    scene.add<A>(abc, A{});
    scene.add<B>(abc, B{});
    scene.add<C>(abc, C{});
    const rb::Entity ab = scene.create();
    scene.add<A>(ab, A{});
    scene.add<B>(ab, B{});
    const rb::Entity ac = scene.create();
    scene.add<A>(ac, A{});
    scene.add<C>(ac, C{});
    scene.add<A>(scene.create(), A{});

    int visited = 0;
    rb::Entity seen;
    scene.each<A, B, C>([&](rb::Entity e, A&, B&, C&) {
        ++visited;
        seen = e;
    });
    CHECK(visited == 1);
    CHECK(seen == abc);
}

int main() {
    smallestPoolDrivesIntersection();
    singleEachVisitsAllAndMutatesInPlace();
    addOtherComponentDuringIteration();
    removeNonDriverComponentDuringIteration();
    deferredDestroyDuringIteration();
    threeComponentQueryVisitsTripleIntersection();
    return rbtest::summary("ecs_query");
}

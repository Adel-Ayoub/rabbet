#include "rabbet/ecs/Scene.h"
#include "tests/Test.h"

#include <cstddef>
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

static void manyEntitiesAndQueries() {
    rb::Scene scene;
    constexpr int kCount = 20000;
    long long expectedSum = 0;
    int expectedB = 0;
    int expectedC = 0;
    for (int i = 0; i < kCount; ++i) {
        const rb::Entity e = scene.create();
        scene.add<A>(e, A{i});
        expectedSum += i;
        if (i % 2 == 0) {
            scene.add<B>(e, B{i});
            ++expectedB;
        }
        if (i % 3 == 0) {
            scene.add<C>(e, C{i});
            ++expectedC;
        }
    }
    CHECK(scene.aliveCount() == static_cast<std::size_t>(kCount));
    CHECK(scene.count<A>() == static_cast<std::size_t>(kCount));
    CHECK(scene.count<B>() == static_cast<std::size_t>(expectedB));
    CHECK(scene.count<C>() == static_cast<std::size_t>(expectedC));

    long long sum = 0;
    int visitedA = 0;
    scene.each<A>([&](rb::Entity, A& a) {
        sum += a.v;
        ++visitedA;
    });
    CHECK(visitedA == kCount);
    CHECK(sum == expectedSum);

    int visitedAB = 0;
    scene.each<A, B>([&](rb::Entity, A&, B&) { ++visitedAB; });
    CHECK(visitedAB == expectedB);
}

static void destroyChurnKeepsPoolsConsistent() {
    rb::Scene scene;
    constexpr int kCount = 10000;
    std::vector<rb::Entity> es;
    es.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        const rb::Entity e = scene.create();
        scene.add<A>(e, A{i});
        es.push_back(e);
    }

    int destroyed = 0;
    for (int i = 0; i < kCount; i += 3) {
        scene.destroy(es[static_cast<std::size_t>(i)]);
        ++destroyed;
    }
    CHECK(scene.aliveCount() == static_cast<std::size_t>(kCount - destroyed));
    CHECK(scene.count<A>() == static_cast<std::size_t>(kCount - destroyed));

    int deadOk = 0;
    int dataOk = 0;
    for (int i = 0; i < kCount; ++i) {
        const rb::Entity e = es[static_cast<std::size_t>(i)];
        if (i % 3 == 0) {
            if (!scene.alive(e) && !scene.has<A>(e)) {
                ++deadOk;
            }
        } else if (scene.alive(e) && scene.get<A>(e).v == i) {
            ++dataOk;
        }
    }
    CHECK(deadOk == destroyed);
    CHECK(dataOk == kCount - destroyed);

    // Recreating reuses the freed slots; the fresh entities carry no stale components.
    std::vector<rb::Entity> fresh;
    int cleanFresh = 0;
    for (int i = 0; i < destroyed; ++i) {
        const rb::Entity e = scene.create();
        if (scene.alive(e) && !scene.has<A>(e)) {
            ++cleanFresh;
        }
    }
    CHECK(cleanFresh == destroyed);
    CHECK(scene.aliveCount() == static_cast<std::size_t>(kCount));
    CHECK(scene.count<A>() == static_cast<std::size_t>(kCount - destroyed));
}

static void repeatedComponentRemovalStaysCorrect() {
    rb::Scene scene;
    constexpr int kCount = 4000;
    std::vector<rb::Entity> es;
    es.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        const rb::Entity e = scene.create();
        scene.add<A>(e, A{i * 7});
        es.push_back(e);
    }

    int removed = 0;
    for (int i = 0; i < kCount; i += 3) {
        scene.remove<A>(es[static_cast<std::size_t>(i)]);
        ++removed;
    }
    CHECK(scene.count<A>() == static_cast<std::size_t>(kCount - removed));

    int ok = 0;
    for (int i = 0; i < kCount; ++i) {
        const rb::Entity e = es[static_cast<std::size_t>(i)];
        if (i % 3 == 0) {
            if (!scene.has<A>(e)) {
                ++ok;
            }
        } else if (scene.has<A>(e) && scene.get<A>(e).v == i * 7) {
            ++ok;
        }
    }
    CHECK(ok == kCount);
}

int main() {
    manyEntitiesAndQueries();
    destroyChurnKeepsPoolsConsistent();
    repeatedComponentRemovalStaysCorrect();
    return rbtest::summary("ecs_stress");
}

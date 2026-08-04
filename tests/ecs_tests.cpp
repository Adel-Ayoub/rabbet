#include "rabbet/ecs/Entity.h"
#include "rabbet/ecs/Pool.h"
#include "rabbet/ecs/Scene.h"

#include "tests/Test.h"

#include <cstddef>
#include <span>
#include <unordered_set>
#include <vector>

static void defaultIsNull() {
    rb::Entity e;
    CHECK(!e.valid());
    CHECK(e == rb::kNullEntity);
    CHECK(e.index() == rb::Entity::kInvalidIndex);
}

static void accessorsAndEquality() {
    const rb::Entity a{3u, 7u};
    CHECK(a.valid());
    CHECK(a.index() == 3u);
    CHECK(a.version() == 7u);

    const rb::Entity sameAsA{3u, 7u};
    const rb::Entity laterVersion{3u, 8u};
    const rb::Entity otherSlot{4u, 7u};
    CHECK(a == sameAsA);
    CHECK(a != laterVersion);
    CHECK(a != otherSlot);
}

static void hashable() {
    std::unordered_set<rb::Entity> set;
    set.insert(rb::Entity{1u, 0u});
    set.insert(rb::Entity{2u, 0u});
    set.insert(rb::Entity{1u, 0u});
    CHECK(set.size() == 2u);
    CHECK(set.contains(rb::Entity{1u, 0u}));
    CHECK(!set.contains(rb::Entity{1u, 1u}));
}

void ecsEntitySuite() {
    defaultIsNull();
    accessorsAndEquality();
    hashable();
}

namespace {

struct Num {
    int value = 0;
};

} // namespace

static void assignContainsGet() {
    rb::Pool<Num> pool;
    CHECK(pool.size() == 0u);
    CHECK(!pool.contains(5u));

    Num& a = pool.assign(5u, Num{42});
    CHECK(pool.contains(5u));
    CHECK(pool.size() == 1u);
    CHECK(a.value == 42);
    CHECK(pool.get(5u).value == 42);
    CHECK(pool.tryGet(5u) != nullptr);
    CHECK(pool.tryGet(6u) == nullptr);
}

static void assignReplacesInPlace() {
    rb::Pool<Num> pool;
    pool.assign(2u, Num{1});
    pool.assign(2u, Num{99});
    CHECK(pool.size() == 1u);
    CHECK(pool.get(2u).value == 99);
}

static void removeSwapsLastIntoHole() {
    rb::Pool<Num> pool;
    pool.assign(0u, Num{10});
    pool.assign(1u, Num{11});
    pool.assign(2u, Num{12});
    CHECK(pool.size() == 3u);

    pool.remove(0u);
    CHECK(pool.size() == 2u);
    CHECK(!pool.contains(0u));
    CHECK(pool.contains(1u));
    CHECK(pool.contains(2u));
    CHECK(pool.get(1u).value == 11);
    CHECK(pool.get(2u).value == 12);
}

static void removeLastElement() {
    rb::Pool<Num> pool;
    pool.assign(0u, Num{10});
    pool.assign(1u, Num{11});
    pool.remove(1u);
    CHECK(pool.size() == 1u);
    CHECK(pool.contains(0u));
    CHECK(!pool.contains(1u));
    CHECK(pool.get(0u).value == 10);
}

static void denseContiguousIteration() {
    rb::Pool<Num> pool;
    pool.assign(7u, Num{1});
    pool.assign(3u, Num{2});
    pool.assign(9u, Num{3});

    int sum = 0;
    for (const Num& n : pool.components()) {
        sum += n.value;
    }
    CHECK(sum == 6);

    const std::span<const rb::IPool::Index> owners = pool.entities();
    CHECK(owners.size() == 3u);
}

void ecsPoolSuite() {
    assignContainsGet();
    assignReplacesInPlace();
    removeSwapsLastIntoHole();
    removeLastElement();
    denseContiguousIteration();
}

namespace {

using Index = rb::IPool::Index;

} // namespace

static void swapPopPreservesOtherElements() {
    rb::Pool<Num> pool;
    for (Index i = 0; i < 6; ++i) {
        pool.assign(i, Num{static_cast<int>(i) * 10});
    }
    pool.remove(2u); // remove a middle slot; the last element swaps into the hole
    CHECK(pool.size() == 5u);
    CHECK(!pool.contains(2u));
    for (const Index i : {0u, 1u, 3u, 4u, 5u}) {
        CHECK(pool.contains(i));
        CHECK(pool.get(i).value == static_cast<int>(i) * 10);
    }
}

static void removeMissingIndexIsNoOp() {
    rb::Pool<Num> pool;
    pool.assign(1u, Num{5});
    pool.remove(99u); // never assigned
    pool.remove(0u);  // never assigned
    CHECK(pool.size() == 1u);
    CHECK(pool.get(1u).value == 5);
}

static void assignReplacesInPlaceSameStorage() {
    rb::Pool<Num> pool;
    Num& first = pool.assign(3u, Num{1});
    Num& again = pool.assign(3u, Num{2});
    CHECK(pool.size() == 1u);
    CHECK(&first == &again); // replace reuses the dense slot, no growth
    CHECK(again.value == 2);
}

static void parallelArraysStayConsistent() {
    rb::Pool<Num> pool;
    pool.assign(4u, Num{40});
    pool.assign(8u, Num{80});
    pool.assign(1u, Num{10});
    pool.remove(8u); // swap-pop reshuffles the dense arrays

    const std::span<const Index> owners = pool.entities();
    const std::span<const Num> comps = pool.components();
    CHECK(owners.size() == comps.size());
    CHECK(owners.size() == 2u);
    bool consistent = true;
    for (std::size_t i = 0; i < owners.size(); ++i) {
        if (pool.get(owners[i]).value != comps[i].value) {
            consistent = false;
        }
    }
    CHECK(consistent);
}

static void reassignAfterRemove() {
    rb::Pool<Num> pool;
    pool.assign(2u, Num{1});
    pool.remove(2u);
    CHECK(!pool.contains(2u));
    Num& n = pool.assign(2u, Num{2});
    CHECK(pool.contains(2u));
    CHECK(pool.size() == 1u);
    CHECK(n.value == 2);
}

static void drainToEmpty() {
    rb::Pool<Num> pool;
    for (Index i = 0; i < 10; ++i) {
        pool.assign(i, Num{static_cast<int>(i)});
    }
    for (Index i = 0; i < 10; ++i) {
        pool.remove(i);
    }
    CHECK(pool.size() == 0u);
    CHECK(pool.empty());
    bool noneLeft = true;
    for (Index i = 0; i < 10; ++i) {
        if (pool.contains(i)) {
            noneLeft = false;
        }
    }
    CHECK(noneLeft);
}

static void largePoolHalfRemovalStaysCorrect() {
    rb::Pool<Num> pool;
    constexpr Index kCount = 5000;
    for (Index i = 0; i < kCount; ++i) {
        pool.assign(i, Num{static_cast<int>(i)});
    }
    CHECK(pool.size() == kCount);

    Index removed = 0;
    for (Index i = 0; i < kCount; i += 2) {
        pool.remove(i);
        ++removed;
    }
    CHECK(pool.size() == kCount - removed);

    int ok = 0;
    for (Index i = 0; i < kCount; ++i) {
        if (i % 2u == 0u) {
            if (!pool.contains(i)) {
                ++ok;
            }
        } else if (pool.contains(i) && pool.get(i).value == static_cast<int>(i)) {
            ++ok;
        }
    }
    CHECK(ok == static_cast<int>(kCount));
}

void ecsPoolInvariantsSuite() {
    swapPopPreservesOtherElements();
    removeMissingIndexIsNoOp();
    assignReplacesInPlaceSameStorage();
    parallelArraysStayConsistent();
    reassignAfterRemove();
    drainToEmpty();
    largePoolHalfRemovalStaysCorrect();
}

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

static void clearInvalidatesOldHandles() {
    rb::Scene scene;
    const rb::Entity before = scene.create();
    scene.clear();
    CHECK(!scene.alive(before)); // a handle taken before clear() is dead

    const rb::Entity after = scene.create();
    CHECK(scene.alive(after));
    CHECK(!(before == after));   // a recycled index returns with a new version
    CHECK(!scene.alive(before)); // ...so the old handle never aliases the new entity
}

void ecsSceneSuite() {
    createAndDestroy();
    recycledIndexBumpsVersion();
    addHasGetRemove();
    staleHandleSeesNoComponents();
    destroyClearsEveryPool();
    queryVisitsIntersectionOnly();
    queryEmptyWhenComponentUnused();
    clearRemovesEverything();
    clearInvalidatesOldHandles();
}

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

void ecsRecycleSuite() {
    freeIndicesReusedLifo();
    versionIncrementsEachRecycle();
    doubleDestroyIsNoOp();
    destroyInvalidIsSafe();
    recycledSlotHasNoLeftoverComponents();
    aliveCountTracksChurn();
}

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

void ecsQuerySuite() {
    smallestPoolDrivesIntersection();
    singleEachVisitsAllAndMutatesInPlace();
    addOtherComponentDuringIteration();
    removeNonDriverComponentDuringIteration();
    deferredDestroyDuringIteration();
    threeComponentQueryVisitsTripleIntersection();
}

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

void ecsStressSuite() {
    manyEntitiesAndQueries();
    destroyChurnKeepsPoolsConsistent();
    repeatedComponentRemovalStaysCorrect();
}

int main() {
    ecsEntitySuite();
    ecsPoolSuite();
    ecsPoolInvariantsSuite();
    ecsSceneSuite();
    ecsRecycleSuite();
    ecsQuerySuite();
    ecsStressSuite();
    return rbtest::summary("ecs");
}

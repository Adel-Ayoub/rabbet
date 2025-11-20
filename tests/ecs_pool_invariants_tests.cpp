#include "rabbet/ecs/Pool.h"
#include "tests/Test.h"

#include <cstddef>
#include <span>

namespace {

struct Num {
    int v = 0;
};

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
        CHECK(pool.get(i).v == static_cast<int>(i) * 10);
    }
}

static void removeMissingIndexIsNoOp() {
    rb::Pool<Num> pool;
    pool.assign(1u, Num{5});
    pool.remove(99u); // never assigned
    pool.remove(0u);  // never assigned
    CHECK(pool.size() == 1u);
    CHECK(pool.get(1u).v == 5);
}

static void assignReplacesInPlaceSameStorage() {
    rb::Pool<Num> pool;
    Num& first = pool.assign(3u, Num{1});
    Num& again = pool.assign(3u, Num{2});
    CHECK(pool.size() == 1u);
    CHECK(&first == &again); // replace reuses the dense slot, no growth
    CHECK(again.v == 2);
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
        if (pool.get(owners[i]).v != comps[i].v) {
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
    CHECK(n.v == 2);
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
        } else if (pool.contains(i) && pool.get(i).v == static_cast<int>(i)) {
            ++ok;
        }
    }
    CHECK(ok == static_cast<int>(kCount));
}

int main() {
    swapPopPreservesOtherElements();
    removeMissingIndexIsNoOp();
    assignReplacesInPlaceSameStorage();
    parallelArraysStayConsistent();
    reassignAfterRemove();
    drainToEmpty();
    largePoolHalfRemovalStaysCorrect();
    return rbtest::summary("ecs_pool_invariants");
}

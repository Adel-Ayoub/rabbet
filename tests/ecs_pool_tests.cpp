#include "rabbet/ecs/Pool.h"
#include "tests/Test.h"

#include <span>

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

int main() {
    assignContainsGet();
    assignReplacesInPlace();
    removeSwapsLastIntoHole();
    removeLastElement();
    denseContiguousIteration();
    return rbtest::summary("ecs_pool");
}

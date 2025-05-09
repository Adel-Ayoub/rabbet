#include "rabbet/ecs/Entity.h"
#include "tests/Test.h"

#include <unordered_set>

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

int main() {
    defaultIsNull();
    accessorsAndEquality();
    hashable();
    return rbtest::summary("ecs_entity");
}

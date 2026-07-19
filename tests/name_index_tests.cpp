#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/NameIndex.h"
#include "tests/Test.h"

#include <string>
#include <vector>

namespace {

// The reference the index must agree with: the scan world.find used to run.
rb::Entity scanFor(rb::Scene& scene, const std::string& name) {
    rb::Entity found = rb::kNullEntity;
    scene.each<rb::Name>([&](rb::Entity e, rb::Name& n) {
        if (!found.valid() && n.value == name) {
            found = e;
        }
    });
    return found;
}

rb::Entity named(rb::Scene& scene, const std::string& name) {
    const rb::Entity e = scene.create();
    scene.add<rb::Name>(e, rb::Name{name});
    return e;
}

void findsByNameAndMissesCleanly() {
    rb::Scene scene;
    const rb::Entity lamp = named(scene, "Lamp");
    (void)named(scene, "Wisp");
    rb::NameIndex index;
    index.rebuild(scene);

    CHECK(index.find("Lamp") == lamp);
    CHECK(!index.find("Ghost").valid());
    CHECK(index.size() == 2u);
}

void duplicateNamesKeepFirstMatch() {
    rb::Scene scene;
    const rb::Entity first = named(scene, "Wisp");
    (void)named(scene, "Wisp");
    (void)named(scene, "Wisp");
    rb::NameIndex index;
    index.rebuild(scene);

    CHECK(index.find("Wisp") == first);
    CHECK(index.find("Wisp") == scanFor(scene, "Wisp"));
    CHECK(index.size() == 1u); // one entry per distinct value
}

void emptyNameIsAValueLikeAnyOther() {
    rb::Scene scene;
    const rb::Entity anonymous = named(scene, "");
    rb::NameIndex index;
    index.rebuild(scene);

    CHECK(index.find("") == anonymous);
    CHECK(index.find("") == scanFor(scene, ""));
}

void rebuildDropsStaleEntries() {
    rb::Scene scene;
    const rb::Entity doomed = named(scene, "Doomed");
    const rb::Entity keeper = named(scene, "Keeper");
    rb::NameIndex index;
    index.rebuild(scene);
    CHECK(index.find("Doomed") == doomed);

    scene.destroy(doomed);
    index.rebuild(scene);
    CHECK(!index.find("Doomed").valid());
    CHECK(index.find("Keeper") == keeper);
    CHECK(index.size() == 1u);
}

void recycledSlotResolvesToTheNewEntity() {
    rb::Scene scene;
    const rb::Entity old = named(scene, "Wisp");
    scene.destroy(old);
    const rb::Entity fresh = named(scene, "Wisp"); // reuses the slot, bumps the version
    CHECK(fresh.index() == old.index());
    CHECK(fresh.version() != old.version());

    rb::NameIndex index;
    index.rebuild(scene);
    CHECK(index.find("Wisp") == fresh);
    CHECK(index.find("Wisp") != old);
}

// Destroys swap-remove inside the pool and reorder it, which is exactly when a stale or
// order-assuming index would diverge from the scan. Deterministic churn, no RNG.
void churnStaysScanIdentical() {
    rb::Scene scene;
    std::vector<rb::Entity> entities;
    const char* names[] = {"Wisp", "Lamp", "Wisp", "Orb", "Wisp", "Lamp", "Orb", "Wisp"};
    for (const char* name : names) {
        entities.push_back(named(scene, name));
    }

    rb::NameIndex index;
    const auto agrees = [&] {
        index.rebuild(scene);
        for (const char* name : {"Wisp", "Lamp", "Orb", "Ghost"}) {
            if (index.find(name) != scanFor(scene, name)) {
                return false;
            }
        }
        return true;
    };
    CHECK(agrees());

    // Kill from the middle and the front, then respawn under a reused name.
    scene.destroy(entities[0]);
    scene.destroy(entities[3]);
    scene.destroy(entities[5]);
    CHECK(agrees());
    (void)named(scene, "Orb");
    (void)named(scene, "Ghost");
    CHECK(agrees());
    scene.destroy(entities[2]);
    scene.destroy(entities[4]);
    scene.destroy(entities[7]);
    CHECK(agrees());
    CHECK(index.find("Wisp") == scanFor(scene, "Wisp"));
}

// The index deliberately answers as of the last rebuild: freshness is the owner's
// per-tick rebuild discipline, and versioned handles make a stale answer read as dead
// rather than aliasing a recycled slot.
void staleIndexAnswersAsOfRebuildTime() {
    rb::Scene scene;
    const rb::Entity wisp = named(scene, "Wisp");
    rb::NameIndex index;
    index.rebuild(scene);

    scene.destroy(wisp);
    CHECK(index.find("Wisp") == wisp);
    CHECK(!scene.alive(index.find("Wisp")));
    index.rebuild(scene);
    CHECK(!index.find("Wisp").valid());
}

void clearEmptiesTheIndex() {
    rb::Scene scene;
    (void)named(scene, "Lamp");
    rb::NameIndex index;
    index.rebuild(scene);
    CHECK(index.size() == 1u);
    index.clear();
    CHECK(index.size() == 0u);
    CHECK(!index.find("Lamp").valid());
}

} // namespace

int main() {
    findsByNameAndMissesCleanly();
    duplicateNamesKeepFirstMatch();
    emptyNameIsAValueLikeAnyOther();
    rebuildDropsStaleEntries();
    recycledSlotResolvesToTheNewEntity();
    churnStaysScanIdentical();
    staleIndexAnswersAsOfRebuildTime();
    clearEmptiesTheIndex();
    return rbtest::summary("name_index");
}

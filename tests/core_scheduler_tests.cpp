#include "rabbet/core/Runtime.h"
#include "rabbet/core/System.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <nlohmann/json.hpp>

namespace {

struct CountingSystem final : rb::System {
    int ticks = 0;
    void onUpdate(rb::Runtime&, float) override { ++ticks; }
};

// Always systems tick every frame; Play systems tick only while the runtime plays.
static void playPhaseGatesSystems() {
    rb::Runtime rt;
    auto& always = rt.addSystem<CountingSystem>();
    auto& play = rt.addSystem<CountingSystem, rb::SystemPhase::Play>();
    rt.start();

    CHECK(!rt.playing());
    rt.tick(0.016f);
    CHECK(always.ticks == 1);
    CHECK(play.ticks == 0); // edit mode: play system stays put

    rt.setPlaying(true);
    rt.tick(0.016f);
    CHECK(always.ticks == 2);
    CHECK(play.ticks == 1); // play mode: both tick

    rt.setPlaying(false);
    rt.tick(0.016f);
    CHECK(always.ticks == 3);
    CHECK(play.ticks == 1); // stopped: play system frozen again
}

// The Play->Stop guarantee: a snapshot taken at Play, restored at Stop, reproduces the
// scene exactly regardless of edits made while playing.
static void snapshotRestoresExactly() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Scene scene;
    const rb::Entity a = scene.create();
    scene.add<rb::Name>(a, rb::Name{"Box"});
    rb::Transform ta;
    ta.position = {1.0f, 2.0f, 3.0f};
    scene.add<rb::Transform>(a, ta);
    const rb::Entity b = scene.create();
    scene.add<rb::Name>(b, rb::Name{"Light"});

    const nlohmann::json snapshot = rb::SceneSerializer::toJson(scene, registry);

    // Simulate gameplay mutating the scene during Play.
    scene.get<rb::Transform>(a).position = {-99.0f, 0.0f, 0.0f};
    const rb::Entity spawned = scene.create();
    scene.add<rb::Name>(spawned, rb::Name{"Spawned"});
    scene.destroy(b);

    // Stop: clear and reload the snapshot. The restored scene must hold the original
    // content (entities, components, values) regardless of edits made while playing.
    scene.clear();
    rb::SceneSerializer::fromJson(snapshot, scene, registry);

    CHECK(scene.aliveCount() == 2u); // the play-time spawn is gone, the destroyed one is back
    bool foundBox = false;
    bool foundLight = false;
    for (const rb::Entity e : scene.entities()) {
        const rb::Name* n = scene.tryGet<rb::Name>(e);
        if (n == nullptr) {
            continue;
        }
        if (n->value == "Box") {
            foundBox = true;
            CHECK(scene.has<rb::Transform>(e));
            CHECK(scene.get<rb::Transform>(e).position.x == 1.0f); // play edit (-99) discarded
        } else if (n->value == "Light") {
            foundLight = true;
        }
    }
    CHECK(foundBox);
    CHECK(foundLight);
}

} // namespace

int main() {
    playPhaseGatesSystems();
    snapshotRestoresExactly();
    return rbtest::summary("core_scheduler");
}

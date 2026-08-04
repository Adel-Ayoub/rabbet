#include "rabbet/core/Clock.h"
#include "rabbet/core/Module.h"
#include "rabbet/core/Resource.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/System.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "rabbet/util/Log.h"

#include "tests/Test.h"

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace {

struct Marker {
    int loaded = 0;
};

class BaseModule final : public rb::Module {
public:
    [[nodiscard]] std::string_view name() const override { return "base"; }
    void configure(rb::Runtime& rt) override {
        if (!rt.hasResource<Marker>()) {
            rt.addResource<Marker>();
        }
        rt.resource<Marker>().loaded += 1;
    }
};

class DependentModule final : public rb::Module {
public:
    [[nodiscard]] std::string_view name() const override { return "dependent"; }
    [[nodiscard]] std::vector<std::string_view> dependencies() const override { return {"base"}; }
    void configure(rb::Runtime& rt) override { rt.resource<Marker>().loaded += 10; }
};

} // namespace

static void loadsWhenDependencySatisfied() {
    rb::Runtime rt;
    rt.loadModule<BaseModule>();
    rt.loadModule<DependentModule>();
    CHECK(rt.isModuleLoaded("base"));
    CHECK(rt.isModuleLoaded("dependent"));
    CHECK(rt.resource<Marker>().loaded == 11);
}

static void throwsWhenDependencyMissing() {
    rb::Runtime rt;
    bool threw = false;
    try {
        rt.loadModule<DependentModule>();
    } catch (const rb::ModuleError&) {
        threw = true;
    }
    CHECK(threw);
    CHECK(!rt.isModuleLoaded("dependent"));
}

static void duplicateLoadIsIdempotent() {
    rb::Runtime rt;
    rt.loadModule<BaseModule>();
    rt.loadModule<BaseModule>();
    CHECK(rt.resource<Marker>().loaded == 1);
}

void coreModuleSuite() {
    loadsWhenDependencySatisfied();
    throwsWhenDependencyMissing();
    duplicateLoadIsIdempotent();
}

namespace {

struct Counter {
    int value = 0;
};

struct Config {
    float scale = 1.0f;
};

} // namespace

static void addAndGet() {
    rb::ResourceRegistry reg;
    CHECK(!reg.has<Counter>());
    CHECK(reg.tryGet<Counter>() == nullptr);

    Counter& c = reg.add<Counter>();
    CHECK(reg.has<Counter>());
    c.value = 7;
    CHECK(reg.get<Counter>().value == 7);
    CHECK(reg.tryGet<Counter>() != nullptr);
}

static void distinctTypesAreIndependent() {
    rb::ResourceRegistry reg;
    reg.add<Counter>().value = 3;
    reg.add<Config>().scale = 2.0f;
    CHECK(reg.get<Counter>().value == 3);
    CHECK(reg.get<Config>().scale == 2.0f);
}

static void addReplacesExisting() {
    rb::ResourceRegistry reg;
    reg.add<Counter>().value = 1;
    reg.add<Counter>().value = 9;
    CHECK(reg.get<Counter>().value == 9);
}

void coreResourceSuite() {
    addAndGet();
    distinctTypesAreIndependent();
    addReplacesExisting();
}

namespace {

struct EventLog {
    std::vector<int> events;
};

class Recorder final : public rb::System {
public:
    explicit Recorder(int id) : m_id(id) {}
    void onStart(rb::Runtime& rt) override { rt.resource<EventLog>().events.push_back(100 + m_id); }
    void onUpdate(rb::Runtime& rt, float) override { rt.resource<EventLog>().events.push_back(200 + m_id); }
    void onStop(rb::Runtime& rt) override { rt.resource<EventLog>().events.push_back(300 + m_id); }

private:
    int m_id;
};

class Stopper final : public rb::System {
public:
    void onUpdate(rb::Runtime& rt, float) override {
        ++m_ticks;
        if (m_ticks >= 3) {
            rt.requestStop();
        }
    }
    [[nodiscard]] int ticks() const { return m_ticks; }

private:
    int m_ticks = 0;
};

struct Captured {
    rb::LogLevel level;
    std::string text;
};

} // namespace

static void frameClockCountsAndResets() {
    rb::FrameClock clock;
    CHECK(clock.frame() == 0u);
    CHECK(clock.delta() == 0.0f);

    clock.tick();
    CHECK(clock.frame() == 1u);
    CHECK(clock.delta() >= 0.0f);

    clock.tick();
    CHECK(clock.frame() == 2u);
    CHECK(clock.elapsed() >= 0.0);

    clock.reset();
    CHECK(clock.frame() == 0u);
    CHECK(clock.delta() == 0.0f);
}

static void lifecycleRunsInOrderThenReverse() {
    rb::Runtime rt;
    rt.addResource<EventLog>();
    rt.addSystem<Recorder>(1);
    rt.addSystem<Recorder>(2);

    rt.start();
    rt.tick(0.016f);
    rt.stop();

    const std::vector<int> expected{101, 102, 201, 202, 302, 301};
    CHECK(rt.resource<EventLog>().events == expected);
}

static void runLoopStopsOnRequest() {
    rb::Runtime rt;
    Stopper& stopper = rt.addSystem<Stopper>();
    rt.run();
    CHECK(stopper.ticks() == 3);
    CHECK(!rt.running());
}

static void sceneAndResourcesReachable() {
    rb::Runtime rt;
    CHECK(rt.scene().aliveCount() == 0u);
    const rb::Entity e = rt.scene().create();
    CHECK(rt.scene().alive(e));

    CHECK(!rt.hasResource<EventLog>());
    rt.addResource<EventLog>();
    CHECK(rt.hasResource<EventLog>());
}

static void logCapturesAndFiltersByLevel() {
    std::vector<Captured> out;
    rb::log::setSink([&out](rb::LogLevel lvl, std::string_view msg) {
        out.push_back({lvl, std::string(msg)});
    });
    rb::log::setLevel(rb::LogLevel::Info);

    rb::log::debug("hidden {}", 1);
    rb::log::info("hello {}", 42);
    rb::log::error("boom {}", 7);

    CHECK(out.size() == 2u);
    CHECK(out[0].level == rb::LogLevel::Info);
    CHECK(out[0].text == "hello 42");
    CHECK(out[1].level == rb::LogLevel::Error);
    CHECK(out[1].text == "boom 7");

    rb::log::resetSink();
    rb::log::setLevel(rb::LogLevel::Info);
}

static void logOffSuppressesEverything() {
    std::vector<Captured> out;
    rb::log::setSink([&out](rb::LogLevel lvl, std::string_view msg) {
        out.push_back({lvl, std::string(msg)});
    });
    rb::log::setLevel(rb::LogLevel::Off);

    rb::log::error("nope");

    CHECK(out.empty());

    rb::log::resetSink();
    rb::log::setLevel(rb::LogLevel::Info);
}

void coreRuntimeSuite() {
    frameClockCountsAndResets();
    lifecycleRunsInOrderThenReverse();
    runLoopStopsOnRequest();
    sceneAndResourcesReachable();
    logCapturesAndFiltersByLevel();
    logOffSuppressesEverything();
}

namespace {

struct CountingSystem final : rb::System {
    int ticks = 0;
    void onUpdate(rb::Runtime&, float) override { ++ticks; }
};

struct PlayEdgeSystem final : rb::System {
    int begins = 0;
    int ends = 0;
    void onPlayBegin(rb::Runtime&) override { ++begins; }
    void onPlayEnd(rb::Runtime&) override { ++ends; }
};

// Play-session edges fire exactly once per begin/end and only for Play-phase systems.
// Pause/Step toggle the per-frame setPlaying() gate instead; a repeated beginPlay()
// must not fire the edge again.
static void playEdgesFireOncePerSession() {
    rb::Runtime rt;
    auto& always = rt.addSystem<PlayEdgeSystem>();
    auto& play = rt.addSystem<PlayEdgeSystem, rb::SystemPhase::Play>();
    rt.start();

    rt.beginPlay();
    rt.beginPlay(); // idempotent
    CHECK(play.begins == 1);
    CHECK(always.begins == 0); // Always systems do not receive play edges

    rt.endPlay();
    rt.endPlay(); // idempotent
    CHECK(play.ends == 1);
    CHECK(always.ends == 0);

    CHECK(rt.inPlaySession() == false);
    rt.beginPlay();
    CHECK(rt.inPlaySession());
    CHECK(play.begins == 2); // a second session begins again
}

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

void coreSchedulerSuite() {
    playPhaseGatesSystems();
    playEdgesFireOncePerSession();
    snapshotRestoresExactly();
}

int main() {
    coreModuleSuite();
    coreResourceSuite();
    coreRuntimeSuite();
    coreSchedulerSuite();
    return rbtest::summary("core");
}

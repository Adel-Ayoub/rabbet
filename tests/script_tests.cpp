#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Clock.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/physics/BoxCollider.h"
#include "rabbet/physics/PhysicsControl.h"
#include "rabbet/physics/PhysicsSystem.h"
#include "rabbet/physics/RigidBody.h"
#include "rabbet/scene/CameraShake.h"
#include "rabbet/scene/CameraShakeSystem.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scripting/ScriptAsset.h"
#include "rabbet/scripting/ScriptAssetResolveSystem.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptSystem.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/Prefab.h"
#include "rabbet/serialize/PrefabInstance.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

namespace {

bool approx(float a, float b) { return std::fabs(a - b) < 1e-3f; }

rb::Uuid addScript(rb::AssetManager& assets, std::string source) {
    const rb::Uuid id = rb::Uuid::generate();
    rb::ScriptAsset asset;
    asset.source = std::move(source); // path left empty: the hot-reload poll is a no-op
    assets.add<rb::ScriptAsset>(std::move(asset), id);
    return id;
}

rb::Entity scriptedEntity(rb::Runtime& runtime, const rb::Uuid& script) {
    const rb::Entity e = runtime.scene().create();
    runtime.scene().add<rb::Transform>(e, rb::Transform{});
    rb::ScriptComponent component;
    component.script = script;
    component.handle = runtime.resource<rb::AssetManager>().find<rb::ScriptAsset>(script);
    runtime.scene().add<rb::ScriptComponent>(e, component);
    return e;
}

float posX(rb::Runtime& runtime, rb::Entity e) {
    return runtime.scene().get<rb::Transform>(e).position.x;
}

rb::Entity findByName(rb::Scene& scene, const std::string& name) {
    rb::Entity found = rb::kNullEntity;
    scene.each<rb::Name>([&](rb::Entity e, rb::Name& n) {
        if (!found.valid() && n.value == name) {
            found = e;
        }
    });
    return found;
}

// The scheduler gates scripts behind Play, and on_update reads fields + frame time to
// move the entity. Driven through Runtime exactly as the editor drives it.
void playGatesAndUpdatesMove() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(
        assets,
        "fields = { speed = 2.0 }\n"
        "function on_update(self, dt) self:translate(fields.speed * dt, 0.0, 0.0) end\n");
    runtime.addSystem<rb::ScriptAssetResolveSystem>();
    runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    const rb::Entity e = scriptedEntity(runtime, id);
    runtime.start();

    runtime.tick(0.5f); // edit mode: Play-phase script is gated, no movement
    CHECK(approx(posX(runtime, e), 0.0f));

    runtime.beginPlay();
    runtime.setPlaying(true);
    for (int i = 0; i < 10; ++i) {
        runtime.tick(0.1f); // 10 x 2.0 * 0.1 (larger ticks would hit the script dt clamp)
    }
    CHECK(approx(posX(runtime, e), 2.0f));

    runtime.setPlaying(false);
    runtime.endPlay();
    runtime.tick(0.5f); // stopped: frozen
    CHECK(approx(posX(runtime, e), 2.0f));

    // The script's declared field was discovered and is now inspectable.
    const rb::ScriptComponent& component = runtime.scene().get<rb::ScriptComponent>(e);
    CHECK(component.fields.size() == 1u);
    CHECK(component.fields[0].name == "speed");
}

// on_start runs once per play session, not every frame.
void onStartRunsOnce() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets, "function on_start(self) self:translate(100.0, 0.0, 0.0) end\n"
                                           "function on_update(self, dt) end\n");
    const rb::Entity e = scriptedEntity(runtime, id);

    rb::ScriptSystem scripts;
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.016f);
    scripts.onUpdate(runtime, 0.016f);
    scripts.onUpdate(runtime, 0.016f);
    CHECK(approx(posX(runtime, e), 100.0f)); // moved once, not three times
}

// An inspector/scene override of a field overrides the script's declared default and
// drives behavior.
void fieldOverrideDrivesScript() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(
        assets, "fields = { speed = 1.0 }\n"
                "function on_update(self, dt) self:translate(fields.speed * dt, 0.0, 0.0) end\n");
    const rb::Entity e = scriptedEntity(runtime, id);

    rb::ScriptField override;
    override.name = "speed";
    override.type = rb::ScriptField::Type::Number;
    override.number = 5.0;
    runtime.scene().get<rb::ScriptComponent>(e).fields.push_back(override);

    rb::ScriptSystem scripts;
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.1f);
    CHECK(approx(posX(runtime, e), 0.5f)); // 5.0 override x 0.1, not the 1.0 default
}

// Bumping the asset revision (what the hot-reload poll does after a file edit) recompiles
// the live instance with the new behavior, no restart.
void revisionBumpHotReloads() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(
        assets, "function on_update(self, dt) self:translate(1.0 * dt, 0.0, 0.0) end\n");
    const rb::Entity e = scriptedEntity(runtime, id);

    rb::ScriptSystem scripts;
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.1f);
    CHECK(approx(posX(runtime, e), 0.1f));

    rb::ScriptAsset& asset = *assets.get<rb::ScriptAsset>(
        runtime.scene().get<rb::ScriptComponent>(e).handle);
    asset.source = "function on_update(self, dt) self:translate(10.0 * dt, 0.0, 0.0) end\n";
    ++asset.revision;

    scripts.onUpdate(runtime, 0.1f); // recompiles, then runs the new on_update
    CHECK(approx(posX(runtime, e), 1.1f));
}

// Reading Input is safe when no Input resource is present (e.g. headless): the query just
// returns false rather than crashing.
void inputWithoutResourceIsSafe() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(
        assets, "function on_update(self, dt) if input.down('W') then self:translate(1,0,0) end end\n");
    const rb::Entity e = scriptedEntity(runtime, id);

    rb::ScriptSystem scripts;
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.016f);
    CHECK(approx(posX(runtime, e), 0.0f));
}

// Script fields of every supported type survive a scene save/load round-trip.
void fieldsSurviveSerialization() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Scene scene;
    const rb::Entity e = scene.create();
    rb::ScriptComponent component;
    component.script = rb::Uuid::generate();
    component.fields.push_back({"speed", rb::ScriptField::Type::Number, 7.5, false, ""});
    component.fields.push_back({"enabled", rb::ScriptField::Type::Boolean, 0.0, true, ""});
    component.fields.push_back({"label", rb::ScriptField::Type::String, 0.0, false, "hi"});
    scene.add<rb::ScriptComponent>(e, component);

    const nlohmann::json doc = rb::SceneSerializer::toJson(scene, registry);
    rb::Scene restored;
    rb::SceneSerializer::fromJson(doc, restored, registry);

    bool found = false;
    for (const rb::Entity entity : restored.entities()) {
        const rb::ScriptComponent* loaded = restored.tryGet<rb::ScriptComponent>(entity);
        if (loaded == nullptr) {
            continue;
        }
        found = true;
        CHECK(loaded->script == component.script);
        CHECK(loaded->fields.size() == 3u);
        for (const rb::ScriptField& field : loaded->fields) {
            if (field.name == "speed") {
                CHECK(field.type == rb::ScriptField::Type::Number);
                CHECK(approx(static_cast<float>(field.number), 7.5f));
            } else if (field.name == "enabled") {
                CHECK(field.type == rb::ScriptField::Type::Boolean);
                CHECK(field.boolean);
            } else if (field.name == "label") {
                CHECK(field.type == rb::ScriptField::Type::String);
                CHECK(field.text == "hi");
            }
        }
    }
    CHECK(found);
}

// world.find resolves another entity by its Name and hands back a live handle a script
// can measure against and drive; a miss returns nil. The mover reads a 3-4-5 distance
// off the target, then repositions it.
void worldFindAndCrossEntityAccess() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets,
                                  "function on_update(self, dt)\n"
                                  "  local target = world.find(\"Target\")\n"
                                  "  if target then\n"
                                  "    self:set_position(self:distance_to(target), 0.0, 0.0)\n"
                                  "    target:set_position(5.0, 6.0, 7.0)\n"
                                  "  end\n"
                                  "  if world.find(\"Nobody\") ~= nil then\n"
                                  "    self:set_position(-1.0, 0.0, 0.0)\n"
                                  "  end\n"
                                  "end\n");
    const rb::Entity mover = scriptedEntity(runtime, id);
    const rb::Entity target = runtime.scene().create();
    rb::Transform where;
    where.position = {3.0f, 4.0f, 0.0f};
    runtime.scene().add<rb::Transform>(target, where);
    runtime.scene().add<rb::Name>(target, rb::Name{"Target"});

    rb::ScriptSystem scripts;
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.016f);

    CHECK(approx(runtime.scene().get<rb::Transform>(target).position.x, 5.0f));
    CHECK(approx(runtime.scene().get<rb::Transform>(target).position.y, 6.0f));
    CHECK(approx(posX(runtime, mover), 5.0f)); // the distance stuck; find("Nobody") was nil
}

// world.destroy is deferred to end-of-tick (the handle stays valid inside the same tick),
// the entity is gone afterwards, and a kept handle reports invalid + infinite distance.
void worldDestroyDefersAndInvalidatesHandles() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid killer = addScript(
        assets,
        "prey = nil\n"
        "function on_update(self, dt)\n"
        "  if prey == nil then\n"
        "    prey = world.find(\"Prey\")\n"
        "    world.destroy(prey)\n"
        "    if prey:valid() then self:translate(1.0, 0.0, 0.0) end\n"
        "  else\n"
        "    if not prey:valid() then self:translate(10.0, 0.0, 0.0) end\n"
        "    if prey:distance_to(prey) == math.huge then self:translate(100.0, 0.0, 0.0) end\n"
        "  end\n"
        "end\n");
    const rb::Entity a = scriptedEntity(runtime, killer);
    const rb::Entity b = runtime.scene().create();
    runtime.scene().add<rb::Transform>(b, rb::Transform{});
    runtime.scene().add<rb::Name>(b, rb::Name{"Prey"});

    rb::ScriptSystem scripts;
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.016f);
    CHECK(approx(posX(runtime, a), 1.0f)); // still valid at destroy time -> deferred
    CHECK(!runtime.scene().alive(b));      // applied once the tick finished
    scripts.onUpdate(runtime, 0.016f);
    CHECK(approx(posX(runtime, a), 111.0f)); // invalid handle + infinite distance
}

// A script can destroy its own entity; the pool is not corrupted and later ticks are safe.
void selfDestroyIsSafe() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid suicide =
        addScript(assets, "function on_update(self, dt) world.destroy(self) end\n");
    const rb::Uuid mover =
        addScript(assets, "function on_update(self, dt) self:translate(1.0, 0.0, 0.0) end\n");
    const rb::Entity doomed = scriptedEntity(runtime, suicide);
    const rb::Entity other = scriptedEntity(runtime, mover);

    rb::ScriptSystem scripts;
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.016f);
    CHECK(!runtime.scene().alive(doomed));
    scripts.onUpdate(runtime, 0.016f);
    CHECK(runtime.scene().alive(other));
    CHECK(approx(posX(runtime, other), 2.0f)); // unaffected, ticked both frames
}

// The physics bindings write into PhysicsCommands and read from PhysicsState, and no-op
// safely when the bridge resources are absent (no PhysicsSystem in the session).
void physicsBindingsUseBridgeResources() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets,
                                  "function on_update(self, dt)\n"
                                  "  self:set_velocity(1.0, 2.0, 3.0)\n"
                                  "  self:impulse(4.0, 5.0, 6.0)\n"
                                  "  local vx, vy, vz = self:velocity()\n"
                                  "  self:set_position(vx, vy, vz)\n"
                                  "end\n");
    const rb::Entity e = scriptedEntity(runtime, id);

    rb::ScriptSystem scripts;
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.016f); // no bridge resources yet: everything no-ops
    CHECK(approx(posX(runtime, e), 0.0f));

    rb::PhysicsCommands& commands = runtime.addResource<rb::PhysicsCommands>();
    rb::PhysicsState& state = runtime.addResource<rb::PhysicsState>();
    state.linearVelocity[e] = {7.0f, 8.0f, 9.0f};
    scripts.onUpdate(runtime, 0.016f);

    CHECK(commands.queue.size() == 2u);
    if (commands.queue.size() == 2u) {
        CHECK(commands.queue[0].entity == e);
        CHECK(commands.queue[0].op == rb::PhysicsCommands::Op::SetVelocity);
        CHECK(approx(commands.queue[0].value.y, 2.0f));
        CHECK(commands.queue[1].op == rb::PhysicsCommands::Op::Impulse);
        CHECK(approx(commands.queue[1].value.z, 6.0f));
    }
    CHECK(approx(posX(runtime, e), 7.0f)); // velocity() read the published state
}

// world.spawn instantiates a catalogued prefab: deferred within the tick, linked to its
// source through a PrefabInstance, placed at the requested position, addressable by the
// friendly stem ("orb" for orb.prefab.json), and a safe no-op for an unknown name.
void worldSpawnInstantiatesPrefab() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "rabbet_script_spawn_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    {
        rb::Scene authoring;
        const rb::Entity source = authoring.create();
        authoring.add<rb::Name>(source, rb::Name{"Orb"});
        authoring.add<rb::Transform>(source, rb::Transform{});
        CHECK(rb::savePrefabToFile(authoring, registry, source, dir / "orb.prefab.json"));
    }

    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AssetDatabase& database = runtime.addResource<rb::AssetDatabase>();
    CHECK(database.scan(dir, &assets) == 1u);

    const rb::Uuid id = addScript(assets,
                                  "did = false\n"
                                  "function on_update(self, dt)\n"
                                  "  if did then return end\n"
                                  "  did = true\n"
                                  "  world.spawn(\"orb\", 1.0, 2.0, 3.0)\n"
                                  "  world.spawn(\"no_such_prefab\", 0.0, 0.0, 0.0)\n"
                                  "  if world.find(\"Orb\") ~= nil then\n"
                                  "    self:set_position(-1.0, 0.0, 0.0)\n"
                                  "  end\n"
                                  "end\n");
    const rb::Entity e = scriptedEntity(runtime, id);

    rb::ScriptSystem scripts(&registry);
    scripts.onPlayBegin(runtime);
    const std::size_t before = runtime.scene().aliveCount();
    scripts.onUpdate(runtime, 0.016f);

    CHECK(runtime.scene().aliveCount() == before + 1u); // one spawned, the bogus one skipped
    CHECK(approx(posX(runtime, e), 0.0f));              // not visible mid-tick (deferred)

    bool found = false;
    for (const rb::Entity spawned : runtime.scene().entities()) {
        const rb::Name* name = runtime.scene().tryGet<rb::Name>(spawned);
        if (name == nullptr || name->value != "Orb") {
            continue;
        }
        found = true;
        const rb::Transform& placed = runtime.scene().get<rb::Transform>(spawned);
        CHECK(approx(placed.position.x, 1.0f));
        CHECK(approx(placed.position.y, 2.0f));
        CHECK(approx(placed.position.z, 3.0f));
        CHECK(runtime.scene().tryGet<rb::PrefabInstance>(spawned) != nullptr);
    }
    CHECK(found);
    fs::remove_all(dir, ec);
}

// A real-world stall (window drag, shader compile) reaches on_update clamped, mirroring
// the particle and terrain steps, so script motion cannot teleport past physics.
void dtSpikeIsClamped() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id =
        addScript(assets, "function on_update(self, dt) self:translate(dt, 0.0, 0.0) end\n");
    runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    const rb::Entity e = scriptedEntity(runtime, id);
    runtime.start();
    runtime.beginPlay();
    runtime.setPlaying(true);

    runtime.tick(5.0f); // a 5s stall arrives as one clamped step

    CHECK(approx(posX(runtime, e), 0.1f));
    runtime.endPlay();
}

// world.destroy takes the whole subtree with it, like the editor's delete: destroying a
// multi-entity (prefab-shaped) root must not orphan its children at reinterpreted poses.
void worldDestroyCascadesTheSubtree() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets,
                                  "function on_update(self, dt)\n"
                                  "  local doomed = world.find(\"Doomed\")\n"
                                  "  if doomed ~= nil then world.destroy(doomed) end\n"
                                  "end\n");
    runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    (void)scriptedEntity(runtime, id);

    rb::Scene& scene = runtime.scene();
    const rb::Entity root = scene.create();
    scene.add<rb::Name>(root, rb::Name{"Doomed"});
    scene.add<rb::Transform>(root, rb::Transform{});
    const rb::Entity child = scene.create();
    scene.add<rb::Transform>(child, rb::Transform{});
    const rb::Entity grand = scene.create();
    CHECK(rb::setParent(scene, child, root));
    CHECK(rb::setParent(scene, grand, child));

    runtime.start();
    runtime.beginPlay();
    runtime.setPlaying(true);
    runtime.tick(0.016f);

    CHECK(!scene.alive(root));
    CHECK(!scene.alive(child));
    CHECK(!scene.alive(grand));
    runtime.endPlay();
}

// Instances of entities destroyed outside world.destroy (an editor delete, a parent's
// cascade) are reaped on the next tick instead of living until Stop.
void externallyDestroyedEntityDropsItsInstance() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets, "function on_update(self, dt) end\n");
    rb::ScriptSystem& system = runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    const rb::Entity first = scriptedEntity(runtime, id);
    (void)scriptedEntity(runtime, id);

    runtime.start();
    runtime.beginPlay();
    runtime.setPlaying(true);
    runtime.tick(0.016f);
    CHECK(system.instanceCount() == 2u);

    runtime.scene().destroy(first); // external: bypasses the world.destroy queue
    runtime.tick(0.016f);
    CHECK(system.instanceCount() == 1u);
    runtime.endPlay();
}

// world.load_scene is deferred to the end of the tick, resolves catalogued Scene assets by
// friendly stem, swaps the live scene in place, and starts the incoming scene's scripts
// fresh. The Play-snapshot workflow (the editor's Stop) still restores the scene that was
// playing when Play was pressed; the swap never touches it.
void worldLoadSceneSwitchesAndStopRestores() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "rabbet_script_load_scene_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AssetDatabase& database = runtime.addResource<rb::AssetDatabase>();

    const rb::Uuid switcher =
        addScript(assets, "function on_update(self, dt) world.load_scene(\"arena\") end\n");
    const rb::Uuid greeter =
        addScript(assets, "function on_start(self) self:set_position(9.0, 0.0, 0.0) end\n");

    { // The title scene: a switcher script plus a marker only it carries.
        rb::Scene authoring;
        const rb::Entity control = authoring.create();
        authoring.add<rb::Name>(control, rb::Name{"TitleControl"});
        authoring.add<rb::Transform>(control, rb::Transform{});
        rb::ScriptComponent script;
        script.script = switcher;
        authoring.add<rb::ScriptComponent>(control, script);
        CHECK(rb::SceneSerializer::saveToFile(authoring, registry, dir / "title.scene.json"));
    }
    { // The arena scene: its own scripted entity, so on_start provably runs after the swap.
        rb::Scene authoring;
        const rb::Entity host = authoring.create();
        authoring.add<rb::Name>(host, rb::Name{"ArenaHost"});
        authoring.add<rb::Transform>(host, rb::Transform{});
        rb::ScriptComponent script;
        script.script = greeter;
        authoring.add<rb::ScriptComponent>(host, script);
        CHECK(rb::SceneSerializer::saveToFile(authoring, registry, dir / "arena.scene.json"));
    }
    CHECK(database.scan(dir, &assets) == 2u);

    runtime.addSystem<rb::ScriptAssetResolveSystem>();
    rb::ScriptSystem& scripts =
        runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>(&registry);
    CHECK(rb::SceneSerializer::loadFromFile(runtime.scene(), registry,
                                            dir / "title.scene.json"));
    runtime.start();

    const nlohmann::json snapshot = rb::SceneSerializer::toJson(runtime.scene(), registry);
    runtime.beginPlay();
    runtime.setPlaying(true);

    runtime.tick(0.016f); // the switcher requests the arena; the swap lands at end of tick
    CHECK(!findByName(runtime.scene(), "TitleControl").valid());
    const rb::Entity host = findByName(runtime.scene(), "ArenaHost");
    CHECK(host.valid());
    CHECK(scripts.instanceCount() == 0u); // outgoing environments dropped with their scene

    runtime.tick(0.016f); // the arena's script resolves, compiles, and runs on_start
    CHECK(approx(posX(runtime, host), 9.0f));
    CHECK(scripts.instanceCount() == 1u);

    runtime.setPlaying(false);
    runtime.endPlay();
    runtime.scene().clear(); // the editor's Stop: restore the snapshot taken at Play
    rb::SceneSerializer::fromJson(snapshot, runtime.scene(), registry);
    CHECK(findByName(runtime.scene(), "TitleControl").valid());
    CHECK(!findByName(runtime.scene(), "ArenaHost").valid());
    fs::remove_all(dir, ec);
}

// Two requests in one tick resolve to the last one, like the last write to a variable; an
// unknown name, and a catalogued file that turns out not to be a scene, both warn and
// leave the live scene untouched.
void worldLoadSceneLastRequestWinsAndUnknownIsSafe() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "rabbet_script_load_last_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    for (const char* name : {"one", "two"}) {
        rb::Scene authoring;
        const rb::Entity marker = authoring.create();
        authoring.add<rb::Name>(marker, rb::Name{std::string("Marker_") + name});
        CHECK(rb::SceneSerializer::saveToFile(authoring, registry,
                                              dir / (std::string(name) + ".scene.json")));
    }
    { // Valid JSON, but no scene inside: catalogued, yet loading it must refuse.
        std::ofstream stub(dir / "hollow.scene.json");
        stub << "{}\n";
    }

    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AssetDatabase& database = runtime.addResource<rb::AssetDatabase>();
    CHECK(database.scan(dir, &assets) == 3u);

    const rb::Uuid id = addScript(assets,
                                  "function on_update(self, dt)\n"
                                  "  world.load_scene(\"one\")\n"
                                  "  world.load_scene(\"two\")\n"
                                  "end\n");
    (void)scriptedEntity(runtime, id);
    rb::ScriptSystem scripts(&registry);
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.016f);
    CHECK(findByName(runtime.scene(), "Marker_two").valid());
    CHECK(!findByName(runtime.scene(), "Marker_one").valid());

    // A fresh scripted entity in the swapped-in scene asks for a scene that does not
    // exist and then for the hollow file: both requests are consumed, nothing changes,
    // nothing accumulates.
    const rb::Uuid bogus =
        addScript(assets, "function on_update(self, dt) world.load_scene(\"nowhere\") end\n");
    (void)scriptedEntity(runtime, bogus);
    const std::size_t before = runtime.scene().aliveCount();
    scripts.onUpdate(runtime, 0.016f);
    scripts.onUpdate(runtime, 0.016f);
    CHECK(runtime.scene().aliveCount() == before);
    CHECK(findByName(runtime.scene(), "Marker_two").valid());

    const rb::Uuid hollow =
        addScript(assets, "function on_update(self, dt) world.load_scene(\"hollow\") end\n");
    (void)scriptedEntity(runtime, hollow);
    scripts.onUpdate(runtime, 0.016f);
    CHECK(runtime.scene().aliveCount() == before + 1u); // only the new script entity
    CHECK(findByName(runtime.scene(), "Marker_two").valid());
    scripts.onPlayEnd(runtime);
    fs::remove_all(dir, ec);
}

// world.spawn under a live session: spawned scripted prefabs compile and run their own
// scripts, and world.find sees them from the next tick on (the name index re-arms every
// tick, not once per session).
void spawnedScriptedWispsRunAndAreFindable() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "rabbet_script_spawn_volume_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AssetDatabase& database = runtime.addResource<rb::AssetDatabase>();

    const rb::Uuid drift =
        addScript(assets, "function on_update(self, dt) self:translate(dt, 0.0, 0.0) end\n");
    {
        rb::Scene authoring;
        const rb::Entity wisp = authoring.create();
        authoring.add<rb::Name>(wisp, rb::Name{"Wisp"});
        authoring.add<rb::Transform>(wisp, rb::Transform{});
        rb::ScriptComponent script;
        script.script = drift;
        authoring.add<rb::ScriptComponent>(wisp, script);
        CHECK(rb::savePrefabToFile(authoring, registry, wisp, dir / "wisp.prefab.json"));
    }
    CHECK(database.scan(dir, &assets) == 1u);

    const rb::Uuid controller = addScript(assets,
                                          "function on_update(self, dt)\n"
                                          "  world.spawn(\"wisp\", 5.0, 0.0, 0.0)\n"
                                          "  if world.find(\"Wisp\") ~= nil then\n"
                                          "    self:translate(1.0, 0.0, 0.0)\n"
                                          "  end\n"
                                          "end\n");

    runtime.addSystem<rb::ScriptAssetResolveSystem>();
    rb::ScriptSystem& scripts =
        runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>(&registry);
    const rb::Entity control = scriptedEntity(runtime, controller);
    runtime.start();
    runtime.beginPlay();
    runtime.setPlaying(true);

    const std::size_t before = runtime.scene().aliveCount();
    for (int i = 0; i < 4; ++i) {
        runtime.tick(0.016f);
    }
    CHECK(runtime.scene().aliveCount() == before + 4u);
    // Spawns land at end of tick, so find() sees the population from tick 2 on.
    CHECK(approx(posX(runtime, control), 3.0f));
    // Every settled wisp runs its own environment; the last tick's batch has not yet.
    CHECK(scripts.instanceCount() == 1u + 3u);
    // The oldest wisp settled at end of tick 1 and has drifted every tick since.
    float maxX = 0.0f;
    runtime.scene().each<rb::Name>([&](rb::Entity e, rb::Name& n) {
        if (n.value == "Wisp") {
            maxX = std::max(maxX, posX(runtime, e));
        }
    });
    CHECK(approx(maxX, 5.0f + 3.0f * 0.016f));

    runtime.setPlaying(false);
    runtime.endPlay();
    CHECK(scripts.instanceCount() == 0u); // the play edge drops every spawned environment
    fs::remove_all(dir, ec);
}

} // namespace

// world.shake writes the resource immediately, the shake system decays it to zero,
// and Stop clears it so a session edge never leaks a wobble into the editor view.
void worldShakeDrivesAndDecays() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets,
                                  "fired = false\n"
                                  "function on_update(self, dt)\n"
                                  "  if not fired then\n"
                                  "    fired = true\n"
                                  "    world.shake(0.5, 0.4)\n"
                                  "  end\n"
                                  "end\n");
    runtime.addSystem<rb::ScriptAssetResolveSystem>();
    // Decay before scripts, the editor's order: a fresh shake survives its first tick.
    runtime.addSystem<rb::CameraShakeSystem, rb::SystemPhase::Play>();
    runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    scriptedEntity(runtime, id);
    runtime.start();

    runtime.beginPlay();
    runtime.setPlaying(true);
    runtime.tick(0.1f);
    CHECK(runtime.hasResource<rb::CameraShake>());
    rb::CameraShake& shake = runtime.resource<rb::CameraShake>();
    CHECK(approx(shake.amplitude, 0.5f));
    // Decay runs before the script loop, so the fired peak survives its first tick.
    CHECK(approx(shake.remaining, 0.4f));
    runtime.tick(0.1f);
    CHECK(approx(shake.remaining, 0.3f)); // the exact rate, not merely "goes down"
    CHECK(approx(shake.time, 0.1f));

    // The offset really moves a live view, and a spent shake is the identity.
    rb::RenderView live;
    rb::applyCameraShake(live, shake);
    CHECK(live.view != glm::mat4(1.0f));
    CHECK(live.projection == glm::mat4(1.0f));
    for (int i = 0; i < 10; ++i) {
        runtime.tick(0.1f);
    }
    CHECK(approx(shake.remaining, 0.0f));
    CHECK(approx(shake.time, 0.0f)); // idle resets the phase for the next shake
    rb::RenderView spent;
    rb::applyCameraShake(spent, shake);
    CHECK(spent.view == glm::mat4(1.0f));

    runtime.setPlaying(false);
    runtime.endPlay();
    CHECK(approx(runtime.resource<rb::CameraShake>().amplitude, 0.0f));
}

// The binding refuses non-finite input and clamps runaway values, so a script bug
// can never poison the view matrix for the session.
void worldShakeRefusesGarbage() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets,
                                  "step = 0\n"
                                  "function on_update(self, dt)\n"
                                  "  step = step + 1\n"
                                  "  if step == 1 then world.shake(0/0, 0.4) end\n"
                                  "  if step == 2 then world.shake(0.5, math.huge) end\n"
                                  "  if step == 3 then world.shake(1e30, 1e30) end\n"
                                  "end\n");
    runtime.addSystem<rb::ScriptAssetResolveSystem>();
    runtime.addSystem<rb::CameraShakeSystem, rb::SystemPhase::Play>();
    runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    scriptedEntity(runtime, id);
    runtime.start();
    runtime.beginPlay();
    runtime.setPlaying(true);

    runtime.tick(0.1f); // NaN amplitude: refused outright
    rb::CameraShake& shake = runtime.resource<rb::CameraShake>();
    CHECK(approx(shake.amplitude, 0.0f));
    CHECK(approx(shake.remaining, 0.0f));

    runtime.tick(0.1f); // inf duration: refused outright
    CHECK(approx(shake.remaining, 0.0f));

    runtime.tick(0.1f); // huge but finite: clamped, then decays normally
    CHECK(approx(shake.amplitude, 5.0f));
    CHECK(approx(shake.remaining, 10.0f));
    rb::RenderView view;
    rb::applyCameraShake(view, shake);
    bool finite = true;
    for (int c = 0; c < 4; ++c) {
        for (int r = 0; r < 4; ++r) {
            finite = finite && std::isfinite(view.view[c][r]);
        }
    }
    CHECK(finite);
}

// on_late_update is the documented late half of the tick: it runs after the physics
// step's Transform write-back, where on_update runs before the step. Probe: both hooks
// stamp x through set_position. The early stamp is overwritten by the same tick's
// write-back on a dynamic body; the late stamp survives to the end of the tick. A late
// pass wrongly ordered before physics would lose both stamps.
void lateUpdateReadsThePostStepPose() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets,
                                  "function on_update(self, dt)\n"
                                  "  local x, y, z = self:position()\n"
                                  "  self:set_position(999.0, y, z)\n"
                                  "end\n"
                                  "function on_late_update(self, dt)\n"
                                  "  local x, y, z = self:position()\n"
                                  "  self:set_position(42.0, y, z)\n"
                                  "end\n");

    const rb::Entity ball = scriptedEntity(runtime, id);
    runtime.scene().get<rb::Transform>(ball).position.y = 5.0f;
    runtime.scene().add<rb::RigidBody>(ball, rb::RigidBody{rb::BodyType::Dynamic});
    runtime.scene().add<rb::BoxCollider>(ball,
                                         rb::BoxCollider{glm::vec3(0.5f), glm::vec3(0.0f)});

    rb::ScriptSystem& scripts = runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>();
    runtime.addSystem<rb::PhysicsSystem, rb::SystemPhase::Play>();
    runtime.addSystem<rb::ScriptLateSystem, rb::SystemPhase::Play>(scripts);
    runtime.start();
    runtime.beginPlay();
    runtime.setPlaying(true);
    for (int i = 0; i < 10; ++i) {
        runtime.tick(rb::kFixedDelta);
    }
    const rb::Transform& transform = runtime.scene().get<rb::Transform>(ball);
    CHECK(approx(transform.position.x, 42.0f)); // the late stamp outlived the write-back
    CHECK(transform.position.y < 5.0f);         // the body fell: a step ran between hooks
    runtime.setPlaying(false);
    runtime.endPlay();
}

int main() {
    playGatesAndUpdatesMove();
    onStartRunsOnce();
    fieldOverrideDrivesScript();
    revisionBumpHotReloads();
    inputWithoutResourceIsSafe();
    fieldsSurviveSerialization();
    worldFindAndCrossEntityAccess();
    worldDestroyDefersAndInvalidatesHandles();
    selfDestroyIsSafe();
    physicsBindingsUseBridgeResources();
    worldSpawnInstantiatesPrefab();
    dtSpikeIsClamped();
    worldDestroyCascadesTheSubtree();
    externallyDestroyedEntityDropsItsInstance();
    worldLoadSceneSwitchesAndStopRestores();
    worldLoadSceneLastRequestWinsAndUnknownIsSafe();
    spawnedScriptedWispsRunAndAreFindable();
    lateUpdateReadsThePostStepPose();
    worldShakeDrivesAndDecays();
    worldShakeRefusesGarbage();
    return rbtest::summary("script");
}

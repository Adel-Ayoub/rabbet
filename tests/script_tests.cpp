#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/physics/PhysicsControl.h"
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

#include <cmath>
#include <filesystem>
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
    runtime.tick(0.5f); // 2.0 * 0.5
    runtime.tick(0.5f); // + 2.0 * 0.5
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
    scripts.onUpdate(runtime, 1.0f);
    CHECK(approx(posX(runtime, e), 5.0f)); // 5.0 override, not the 1.0 default
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
    scripts.onUpdate(runtime, 1.0f);
    CHECK(approx(posX(runtime, e), 1.0f));

    rb::ScriptAsset& asset = *assets.get<rb::ScriptAsset>(
        runtime.scene().get<rb::ScriptComponent>(e).handle);
    asset.source = "function on_update(self, dt) self:translate(10.0 * dt, 0.0, 0.0) end\n";
    ++asset.revision;

    scripts.onUpdate(runtime, 1.0f); // recompiles, then runs the new on_update
    CHECK(approx(posX(runtime, e), 11.0f));
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

// world.find resolves another entity by its Name and hands back the same live handle as
// `self`, so a script can read and drive other entities' transforms; a miss returns nil.
void worldFindAndCrossEntityAccess() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets,
                                  "function on_update(self, dt)\n"
                                  "  local target = world.find(\"Target\")\n"
                                  "  if target then target:set_position(5.0, 6.0, 7.0) end\n"
                                  "  if world.find(\"Nobody\") ~= nil then\n"
                                  "    self:set_position(-1.0, 0.0, 0.0)\n"
                                  "  end\n"
                                  "end\n");
    const rb::Entity mover = scriptedEntity(runtime, id);
    const rb::Entity target = runtime.scene().create();
    runtime.scene().add<rb::Transform>(target, rb::Transform{});
    runtime.scene().add<rb::Name>(target, rb::Name{"Target"});

    rb::ScriptSystem scripts;
    scripts.onPlayBegin(runtime);
    scripts.onUpdate(runtime, 0.016f);

    CHECK(approx(runtime.scene().get<rb::Transform>(target).position.x, 5.0f));
    CHECK(approx(runtime.scene().get<rb::Transform>(target).position.y, 6.0f));
    CHECK(approx(posX(runtime, mover), 0.0f)); // find("Nobody") returned nil
}

// distance_to measures between two live entities (3-4-5 triangle from the origin).
void distanceQueryBetweenEntities() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = addScript(assets,
                                  "function on_update(self, dt)\n"
                                  "  local target = world.find(\"Target\")\n"
                                  "  self:set_position(self:distance_to(target), 0.0, 0.0)\n"
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
    CHECK(approx(posX(runtime, mover), 5.0f));
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

} // namespace

int main() {
    playGatesAndUpdatesMove();
    onStartRunsOnce();
    fieldOverrideDrivesScript();
    revisionBumpHotReloads();
    inputWithoutResourceIsSafe();
    fieldsSurviveSerialization();
    worldFindAndCrossEntityAccess();
    distanceQueryBetweenEntities();
    worldDestroyDefersAndInvalidatesHandles();
    selfDestroyIsSafe();
    physicsBindingsUseBridgeResources();
    worldSpawnInstantiatesPrefab();
    return rbtest::summary("script");
}

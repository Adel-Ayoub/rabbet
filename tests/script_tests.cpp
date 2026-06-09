#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/scripting/ScriptAsset.h"
#include "rabbet/scripting/ScriptAssetResolveSystem.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptSystem.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <cmath>
#include <string>
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

} // namespace

int main() {
    playGatesAndUpdatesMove();
    onStartRunsOnce();
    fieldOverrideDrivesScript();
    revisionBumpHotReloads();
    inputWithoutResourceIsSafe();
    fieldsSurviveSerialization();
    return rbtest::summary("script");
}

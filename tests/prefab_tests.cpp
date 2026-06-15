#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/Prefab.h"
#include "rabbet/serialize/PrefabAsset.h"
#include "rabbet/serialize/PrefabAssetResolveSystem.h"
#include "rabbet/serialize/PrefabInstance.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace {

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

rb::Entity spawnLamp(rb::Scene& scene) {
    const rb::Entity e = scene.create();
    scene.add<rb::Name>(e, rb::Name{"Lamp"});
    rb::Transform t;
    t.position = {1.0f, 2.0f, 3.0f};
    scene.add<rb::Transform>(e, t);
    rb::PointLight light;
    light.intensity = 4.0f;
    scene.add<rb::PointLight>(e, light);
    return e;
}

rb::PrefabAsset prefabFromEntity(rb::Scene& scene, const rb::ComponentRegistry& registry,
                                 rb::Entity e) {
    rb::PrefabAsset prefab;
    prefab.components = rb::entityToPrefabJson(scene, registry, e).at("components");
    return prefab;
}

} // namespace

// Serializing an entity captures its registered components under "components" but never the
// PrefabInstance link, so a prefab made from an instance does not bake in a reference.
static void entityToPrefabExcludesInstanceLink() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity e = spawnLamp(scene);
    scene.add<rb::PrefabInstance>(e, rb::PrefabInstance{rb::Uuid::generate(), {}});

    const nlohmann::json doc = rb::entityToPrefabJson(scene, registry, e);
    CHECK(doc.contains("version"));
    CHECK(doc.contains("components"));
    const nlohmann::json& components = doc.at("components");
    CHECK(components.contains("Name"));
    CHECK(components.contains("Transform"));
    CHECK(components.contains("PointLight"));
    CHECK(!components.contains("PrefabInstance")); // link excluded
}

// Instantiating a prefab creates a new entity carrying copies of the prefab's components.
static void instantiateRecreatesComponents() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene authoring;
    const rb::PrefabAsset prefab = prefabFromEntity(authoring, registry, spawnLamp(authoring));

    rb::Scene scene;
    const rb::Entity e = rb::instantiatePrefab(scene, registry, prefab);

    CHECK(scene.alive(e));
    CHECK(scene.has<rb::Name>(e));
    CHECK(scene.has<rb::Transform>(e));
    CHECK(scene.has<rb::PointLight>(e));
    if (scene.has<rb::Name>(e)) {
        CHECK(scene.get<rb::Name>(e).value == "Lamp");
    }
    if (scene.has<rb::Transform>(e)) {
        CHECK(scene.get<rb::Transform>(e).position.y == 2.0f);
    }
    if (scene.has<rb::PointLight>(e)) {
        CHECK(scene.get<rb::PointLight>(e).intensity == 4.0f);
    }
}

// Reverting an instance restores the prefab's component data, discards local overrides, and
// removes components added after instantiation — while keeping the PrefabInstance link.
static void applyPrefabRevertsOverrides() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene authoring;
    const rb::PrefabAsset prefab = prefabFromEntity(authoring, registry, spawnLamp(authoring));

    rb::Scene scene;
    const rb::Entity e = rb::instantiatePrefab(scene, registry, prefab);
    scene.add<rb::PrefabInstance>(e, rb::PrefabInstance{rb::Uuid::generate(), {}});

    scene.get<rb::Transform>(e).position.y = 99.0f; // local override
    scene.add<rb::Camera>(e, rb::Camera{});         // component added after instantiation

    rb::applyPrefab(scene, registry, e, prefab);

    CHECK(scene.get<rb::Transform>(e).position.y == 2.0f); // override reverted
    CHECK(!scene.has<rb::Camera>(e));                      // extra component removed
    CHECK(scene.has<rb::PrefabInstance>(e));               // link preserved
    CHECK(scene.has<rb::Name>(e));
}

// The resolve system links a PrefabInstance's uuid to its asset handle; an unknown prefab stays
// unresolved.
static void resolveLinksInstance() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = rb::Uuid::generate();
    const rb::AssetHandle<rb::PrefabAsset> prefab = assets.add<rb::PrefabAsset>(rb::PrefabAsset{}, id);

    const rb::Entity known = runtime.scene().create();
    runtime.scene().add<rb::PrefabInstance>(known, rb::PrefabInstance{id, {}});
    const rb::Entity missing = runtime.scene().create();
    runtime.scene().add<rb::PrefabInstance>(missing, rb::PrefabInstance{rb::Uuid::generate(), {}});

    rb::PrefabAssetResolveSystem system;
    system.onUpdate(runtime, 0.016f);

    CHECK(runtime.scene().get<rb::PrefabInstance>(known).handle == prefab);
    CHECK(runtime.scene().get<rb::PrefabInstance>(known).handle.valid());
    CHECK(!runtime.scene().get<rb::PrefabInstance>(missing).handle.valid());
}

// A PrefabInstance link survives a scene save/load through the registry; the handle is not
// serialised (it re-resolves on load).
static void instanceSceneRoundTrip() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene source;
    const rb::Entity e = source.create();
    const rb::Uuid id = rb::Uuid::generate();
    source.add<rb::PrefabInstance>(e, rb::PrefabInstance{id, {}});

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    CHECK(loaded.count<rb::PrefabInstance>() == 1u);
    bool found = false;
    loaded.each<rb::PrefabInstance>([&](rb::Entity, rb::PrefabInstance& instance) {
        found = true;
        CHECK(instance.prefab == id);
        CHECK(!instance.handle.valid());
    });
    CHECK(found);
}

// Prefab filenames are sanitized (path separators etc. neutralised, trimmed, never empty) and a
// new prefab never silently overwrites an existing file — it de-collides with _1, _2, ...
static void prefabFilenameSafety() {
    CHECK(rb::sanitizePrefabName("Lamp") == "Lamp");
    CHECK(rb::sanitizePrefabName("a/b\\c:d") == "a_b_c_d");
    CHECK(rb::sanitizePrefabName("   ") == "Prefab");
    CHECK(rb::sanitizePrefabName("") == "Prefab");
    CHECK(rb::sanitizePrefabName("  Point Light  ") == "Point Light"); // inner space kept, edges trimmed
    CHECK(rb::sanitizePrefabName("..hidden") == "hidden");             // leading dots trimmed

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "rabbet_prefab_unique_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    const fs::path first = rb::prefabFilePath(dir, "Box");
    CHECK(first == dir / "Box.prefab.json");
    { std::ofstream(first) << "{}\n"; } // occupy the name
    const fs::path second = rb::prefabFilePath(dir, "Box");
    CHECK(second == dir / "Box_1.prefab.json"); // de-collided, not overwritten

    fs::remove_all(dir, ec);
}

int main() {
    entityToPrefabExcludesInstanceLink();
    instantiateRecreatesComponents();
    applyPrefabRevertsOverrides();
    resolveLinksInstance();
    instanceSceneRoundTrip();
    prefabFilenameSafety();
    return rbtest::summary("prefab");
}

#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Hierarchy.h"
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

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

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

// Root "Post" carrying two children (Head, Base) and a light under the head: the smallest
// tree with both a sibling pair and a 3-deep chain.
rb::Entity spawnLampPost(rb::Scene& scene) {
    const rb::Entity post = scene.create();
    scene.add<rb::Name>(post, rb::Name{"Post"});
    rb::Transform t;
    t.position = {5.0f, 0.0f, 0.0f};
    scene.add<rb::Transform>(post, t);

    const rb::Entity head = scene.create();
    scene.add<rb::Name>(head, rb::Name{"Head"});
    rb::Transform ht;
    ht.position = {0.0f, 2.0f, 0.0f};
    ht.scale = {0.5f, 0.5f, 0.5f};
    scene.add<rb::Transform>(head, ht);
    CHECK(rb::setParent(scene, head, post));

    const rb::Entity base = scene.create();
    scene.add<rb::Name>(base, rb::Name{"Base"});
    rb::Transform bt;
    bt.position = {0.0f, 0.1f, 0.0f};
    scene.add<rb::Transform>(base, bt);
    CHECK(rb::setParent(scene, base, post));

    const rb::Entity glow = scene.create();
    scene.add<rb::Name>(glow, rb::Name{"Glow"});
    rb::Transform gt;
    gt.position = {0.0f, 0.4f, 0.0f};
    scene.add<rb::Transform>(glow, gt);
    rb::PointLight light;
    light.intensity = 7.0f;
    scene.add<rb::PointLight>(glow, light);
    CHECK(rb::setParent(scene, glow, head));
    return post;
}

rb::Entity childNamed(rb::Scene& scene, rb::Entity parent, const std::string& name) {
    for (const rb::Entity child : rb::childrenOf(scene, parent)) {
        const rb::Name* n = scene.tryGet<rb::Name>(child);
        if (n != nullptr && n->value == name) {
            return child;
        }
    }
    return rb::kNullEntity;
}

rb::PrefabAsset prefabFromEntity(rb::Scene& scene, const rb::ComponentRegistry& registry,
                                 rb::Entity e) {
    rb::PrefabAsset prefab;
    prefab.entities = rb::entityToPrefabJson(scene, registry, e).at("entities");
    return prefab;
}

} // namespace

// Serializing captures the subtree's registered components but never a PrefabInstance link, on
// the root or any descendant, so a prefab made from instances does not bake in references.
static void entityToPrefabExcludesInstanceLink() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity e = spawnLamp(scene);
    scene.add<rb::PrefabInstance>(e, rb::PrefabInstance{rb::Uuid::generate(), {}});
    const rb::Entity child = scene.create();
    scene.add<rb::Name>(child, rb::Name{"Shade"});
    scene.add<rb::PrefabInstance>(child, rb::PrefabInstance{rb::Uuid::generate(), {}});
    CHECK(rb::setParent(scene, child, e));

    const nlohmann::json doc = rb::entityToPrefabJson(scene, registry, e);
    CHECK(doc.contains("version"));
    CHECK(doc.contains("entities"));
    CHECK(doc.at("entities").size() == 2u);
    const nlohmann::json& components = doc.at("entities").at(0).at("components");
    CHECK(components.contains("Name"));
    CHECK(components.contains("Transform"));
    CHECK(components.contains("PointLight"));
    CHECK(!components.contains("PrefabInstance")); // link excluded on the root
    const nlohmann::json& childComponents = doc.at("entities").at(1).at("components");
    CHECK(childComponents.contains("Name"));
    CHECK(!childComponents.contains("PrefabInstance")); // and on every descendant
}

// A captured subtree lists one record per entity: sequential ids, the root first without a
// parent ref, and children pointing at their parent's record position.
static void captureWritesSubtreeRecords() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity post = spawnLampPost(scene);

    const nlohmann::json doc = rb::entityToPrefabJson(scene, registry, post);
    CHECK(doc.at("version").get<int>() == 2);
    const nlohmann::json& entities = doc.at("entities");
    CHECK(entities.size() == 4u);
    for (std::size_t i = 0; i < entities.size(); ++i) {
        CHECK(entities.at(i).at("id").get<std::size_t>() == i);
    }
    CHECK(!entities.at(0).contains("parent")); // the root record
    CHECK(entities.at(0).at("components").at("Name").at("value").get<std::string>() == "Post");
    CHECK(entities.at(1).at("components").at("Name").at("value").get<std::string>() == "Head");
    CHECK(entities.at(1).at("parent").get<std::size_t>() == 0u);
    CHECK(entities.at(2).at("components").at("Name").at("value").get<std::string>() == "Base");
    CHECK(entities.at(2).at("parent").get<std::size_t>() == 0u);
    CHECK(entities.at(3).at("components").at("Name").at("value").get<std::string>() == "Glow");
    CHECK(entities.at(3).at("parent").get<std::size_t>() == 1u); // under the head, not the root
    CHECK(entities.at(3).at("components").contains("PointLight"));
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

// Instantiating a subtree prefab rebuilds the whole tree in a fresh scene: parents remapped to
// the new entities, local transforms preserved, and the returned root composing world poses.
static void instantiateRebuildsTree() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene authoring;
    const rb::PrefabAsset prefab = prefabFromEntity(authoring, registry, spawnLampPost(authoring));

    rb::Scene scene;
    const rb::Entity root = rb::instantiatePrefab(scene, registry, prefab);

    CHECK(scene.alive(root));
    CHECK(scene.get<rb::Name>(root).value == "Post");
    CHECK(!rb::parentOf(scene, root).valid());
    CHECK(rb::childrenOf(scene, root).size() == 2u);

    const rb::Entity head = childNamed(scene, root, "Head");
    const rb::Entity base = childNamed(scene, root, "Base");
    CHECK(head.valid());
    CHECK(base.valid());
    const rb::Entity glow = childNamed(scene, head, "Glow");
    CHECK(glow.valid());
    CHECK(rb::parentOf(scene, glow) == head);
    CHECK(scene.get<rb::PointLight>(glow).intensity == 7.0f);
    CHECK(scene.get<rb::Transform>(head).position.y == 2.0f); // locals, not baked world values

    // Post at x 5; head local y 2; glow local y 0.4 under the head's 0.5 scale -> world y 2.2.
    const rb::WorldPose pose = rb::worldPoseOf(scene, glow);
    CHECK(approx(pose.position.x, 5.0f));
    CHECK(approx(pose.position.y, 2.2f));
}

// Reverting an instance restores the prefab's component data, discards local overrides, and
// removes components added after instantiation, while keeping the PrefabInstance link.
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

// Reverting a subtree instance rebuilds the whole tree from the asset: a destroyed prefab child
// comes back, an entity parented in after instantiation goes, and the link stays on the root.
static void revertRebuildsSubtree() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene authoring;
    const rb::PrefabAsset prefab = prefabFromEntity(authoring, registry, spawnLampPost(authoring));

    rb::Scene scene;
    const rb::Entity root = rb::instantiatePrefab(scene, registry, prefab);
    scene.add<rb::PrefabInstance>(root, rb::PrefabInstance{rb::Uuid::generate(), {}});

    scene.get<rb::Transform>(root).position.x = 99.0f;
    const rb::Entity head = childNamed(scene, root, "Head");
    rb::destroySubtree(scene, childNamed(scene, head, "Glow"));
    const rb::Entity extra = scene.create();
    scene.add<rb::Name>(extra, rb::Name{"Extra"});
    CHECK(rb::setParent(scene, extra, head));

    rb::applyPrefab(scene, registry, root, prefab);

    CHECK(scene.alive(root));
    CHECK(scene.has<rb::PrefabInstance>(root));
    CHECK(scene.get<rb::Transform>(root).position.x == 5.0f); // override reverted
    CHECK(rb::childrenOf(scene, root).size() == 2u);
    const rb::Entity newHead = childNamed(scene, root, "Head");
    CHECK(newHead.valid());
    const rb::Entity newGlow = childNamed(scene, newHead, "Glow");
    CHECK(newGlow.valid()); // the destroyed prefab child is rebuilt
    CHECK(scene.get<rb::PointLight>(newGlow).intensity == 7.0f);
    int extras = 0;
    scene.each<rb::Name>([&](rb::Entity, rb::Name& n) {
        if (n.value == "Extra") {
            ++extras;
        }
    });
    CHECK(extras == 0); // the foreign addition went with the replaced subtree
}

// Reverting against an empty asset (a truncated or unreadable file parses to no records)
// must be a no-op, not a wipe: the instance keeps its components, children, and link.
static void revertAgainstEmptyAssetIsANoOp() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene authoring;
    const rb::PrefabAsset prefab = prefabFromEntity(authoring, registry, spawnLampPost(authoring));

    rb::Scene scene;
    const rb::Entity root = rb::instantiatePrefab(scene, registry, prefab);
    scene.add<rb::PrefabInstance>(root, rb::PrefabInstance{rb::Uuid::generate(), {}});

    rb::applyPrefab(scene, registry, root, rb::PrefabAsset{});

    CHECK(scene.alive(root));
    CHECK(scene.has<rb::Name>(root));
    CHECK(scene.has<rb::Transform>(root));
    CHECK(scene.has<rb::PrefabInstance>(root));
    CHECK(rb::childrenOf(scene, root).size() == 2u); // subtree untouched
}

// Reverting never disturbs where the instance sits in the scene: a root parented under another
// entity stays there, the Parent link being structural rather than a registered component.
static void revertKeepsScenePlacement() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene authoring;
    const rb::PrefabAsset prefab = prefabFromEntity(authoring, registry, spawnLampPost(authoring));

    rb::Scene scene;
    const rb::Entity rig = scene.create();
    scene.add<rb::Name>(rig, rb::Name{"Rig"});
    scene.add<rb::Transform>(rig, rb::Transform{});
    const rb::Entity root = rb::instantiatePrefab(scene, registry, prefab);
    scene.add<rb::PrefabInstance>(root, rb::PrefabInstance{rb::Uuid::generate(), {}});
    CHECK(rb::setParent(scene, root, rig));

    rb::destroySubtree(scene, childNamed(scene, root, "Head")); // damage the instance
    rb::applyPrefab(scene, registry, root, prefab);

    CHECK(rb::parentOf(scene, root) == rig); // still mounted on the rig
    CHECK(rb::childrenOf(scene, rig).size() == 1u);
    CHECK(childNamed(scene, root, "Head").valid()); // and repaired from the asset
}

// The older single-entity shape ({version, components}) still loads: it normalizes to a
// one-record subtree and instantiates exactly as before.
static void oldSingleEntityShapeLoads() {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "rabbet_prefab_oldshape_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path file = dir / "relic.prefab.json";
    {
        std::ofstream out(file);
        out << R"({
  "version": 1,
  "components": {
    "Name": { "value": "Relic" },
    "Transform": {
      "position": [1.0, 2.0, 3.0],
      "rotation": [1.0, 0.0, 0.0, 0.0],
      "scale": [1.0, 1.0, 1.0]
    }
  }
})";
    }

    rb::AssetManager assets;
    const rb::AssetHandle<rb::PrefabAsset> handle = rb::loadPrefabAsset(assets, file);
    CHECK(handle.valid());
    const rb::PrefabAsset* prefab = assets.get<rb::PrefabAsset>(handle);
    CHECK(prefab != nullptr);
    if (prefab != nullptr) {
        CHECK(prefab->entities.size() == 1u);

        const rb::ComponentRegistry registry = makeRegistry();
        rb::Scene scene;
        const rb::Entity e = rb::instantiatePrefab(scene, registry, *prefab);
        CHECK(scene.alive(e));
        CHECK(scene.get<rb::Name>(e).value == "Relic");
        CHECK(scene.get<rb::Transform>(e).position.z == 3.0f);
        CHECK(rb::childrenOf(scene, e).empty());
    }
    fs::remove_all(dir, ec);
}

// A subtree survives the full file trip: save, load, instantiate, capture again, and the
// captured document matches the original byte for byte in value terms.
static void savedFileRoundTrips() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene authoring;
    const rb::Entity post = spawnLampPost(authoring);
    const nlohmann::json original = rb::entityToPrefabJson(authoring, registry, post);

    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path() / "rabbet_prefab_roundtrip_test";
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path file = dir / "post.prefab.json";
    CHECK(rb::savePrefabToFile(authoring, registry, post, file));

    rb::AssetManager assets;
    const rb::AssetHandle<rb::PrefabAsset> handle = rb::loadPrefabAsset(assets, file);
    const rb::PrefabAsset* prefab = assets.get<rb::PrefabAsset>(handle);
    CHECK(prefab != nullptr);
    if (prefab != nullptr) {
        rb::Scene scene;
        const rb::Entity root = rb::instantiatePrefab(scene, registry, *prefab);
        CHECK(rb::entityToPrefabJson(scene, registry, root) == original);
    }
    fs::remove_all(dir, ec);
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

// The committed sample nested prefab instantiates as a real tree: pole and head under the post,
// the light under the head, sitting at the head's composed height.
static void sampleLampPostInstantiates() {
    namespace fs = std::filesystem;
    const fs::path file = fs::path(RB_FORGE_ASSETS) / "prefabs" / "lamp_post.prefab.json";

    rb::AssetManager assets;
    const rb::AssetHandle<rb::PrefabAsset> handle = rb::loadPrefabAsset(assets, file);
    CHECK(handle.valid());
    const rb::PrefabAsset* prefab = assets.get<rb::PrefabAsset>(handle);
    CHECK(prefab != nullptr);
    if (prefab == nullptr) {
        return;
    }

    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity root = rb::instantiatePrefab(scene, registry, *prefab);
    CHECK(scene.alive(root));
    CHECK(scene.get<rb::Name>(root).value == "Lamp Post");
    CHECK(rb::childrenOf(scene, root).size() == 2u);

    const rb::Entity pole = childNamed(scene, root, "Pole");
    const rb::Entity head = childNamed(scene, root, "Head");
    CHECK(pole.valid());
    CHECK(head.valid());
    const rb::Entity glow = childNamed(scene, head, "Glow");
    CHECK(glow.valid());
    CHECK(scene.has<rb::PointLight>(glow));

    const rb::WorldPose pose = rb::worldPoseOf(scene, glow);
    CHECK(approx(pose.position.y, 1.95f)); // the lamp light rides the head
}

int main() {
    entityToPrefabExcludesInstanceLink();
    captureWritesSubtreeRecords();
    instantiateRecreatesComponents();
    instantiateRebuildsTree();
    applyPrefabRevertsOverrides();
    revertRebuildsSubtree();
    revertAgainstEmptyAssetIsANoOp();
    revertKeepsScenePlacement();
    oldSingleEntityShapeLoads();
    savedFileRoundTrips();
    resolveLinksInstance();
    instanceSceneRoundTrip();
    prefabFilenameSafety();
    sampleLampPostInstantiates();
    return rbtest::summary("prefab");
}

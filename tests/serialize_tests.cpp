#include "rabbet/assets/AssetHandle.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/Primitive.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Parent.h"
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
#include <string_view>
#include <system_error>
#include <vector>

namespace {

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

// Every builtin carries the full set of reflection hooks the editor relies on, and
// no draw hook until the editor attaches one (the core stays UI-free).
static void builtinsExposeEditorHooks() {
    const rb::ComponentRegistry registry = makeRegistry();
    constexpr std::string_view names[] = {
        "Name",          "Transform",       "Camera",          "DirectionalLight",
        "PointLight",    "SpotLight",       "ModelRenderer",   "MaterialComponent",
        "Primitive",     "ScriptComponent", "RigidBody",       "BoxCollider",
        "SphereCollider", "SoundEmitter",   "PrefabInstance",  "ParticleEmitter",
        "PostProcess",    "TerrainComponent", "SkyboxComponent", "WaterComponent"};
    for (const std::string_view name : names) {
        const rb::ComponentRegistry::Entry* entry = registry.find(name);
        CHECK(entry != nullptr);
        if (entry == nullptr) {
            continue;
        }
        CHECK(entry->has != nullptr);
        CHECK(entry->addDefault != nullptr);
        CHECK(entry->remove != nullptr);
        CHECK(entry->drawInspector == nullptr);
    }
    CHECK(registry.entries().size() == 20u);
}

// addDefault attaches a default-constructed component; it is idempotent.
static void addDefaultAddsThenIsIdempotent() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity e = scene.create();
    const rb::ComponentRegistry::Entry* transform = registry.find("Transform");
    CHECK(transform != nullptr);

    CHECK(!transform->has(scene, e));
    transform->addDefault(scene, e);
    CHECK(transform->has(scene, e));

    scene.get<rb::Transform>(e).position.x = 4.0f;
    transform->addDefault(scene, e); // present already: must not overwrite
    CHECK(scene.get<rb::Transform>(e).position.x == 4.0f);
}

// remove takes the component away and is safe to call when it is absent.
static void removeClearsComponent() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity e = scene.create();
    const rb::ComponentRegistry::Entry* point = registry.find("PointLight");
    CHECK(point != nullptr);

    point->remove(scene, e); // absent: no-op, must not crash
    point->addDefault(scene, e);
    CHECK(point->has(scene, e));
    point->remove(scene, e);
    CHECK(!point->has(scene, e));
}

// setDrawer attaches an editor hook by name and ignores unknown names.
static void setDrawerAttachesByName() {
    rb::ComponentRegistry registry = makeRegistry();
    const rb::ComponentRegistry::DrawFn drawer = +[](rb::Scene&, rb::Entity) {};

    registry.setDrawer("Transform", drawer);
    const rb::ComponentRegistry::Entry* transform = registry.find("Transform");
    CHECK(transform != nullptr);
    CHECK(transform->drawInspector == drawer);

    registry.setDrawer("Nonexistent", drawer); // unknown: no-op, must not crash
}

} // namespace

void serializeRegistrySuite() {
    builtinsExposeEditorHooks();
    addDefaultAddsThenIsIdempotent();
    removeClearsComponent();
    setDrawerAttachesByName();
}

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) { return std::fabs(a - b) <= eps; }

rb::Entity spawnHero(rb::Scene& scene) {
    const rb::Entity e = scene.create();
    scene.add<rb::Name>(e, rb::Name{"hero"});
    rb::Transform t;
    t.position = {1.0f, 2.0f, 3.0f};
    t.scale = {2.0f, 2.0f, 2.0f};
    scene.add<rb::Transform>(e, t);
    return e;
}

rb::Entity spawnNamed(rb::Scene& scene, const std::string& name) {
    const rb::Entity e = scene.create();
    scene.add<rb::Name>(e, rb::Name{name});
    return e;
}

rb::Entity firstNamed(rb::Scene& scene, const std::string& name) {
    rb::Entity found;
    scene.each<rb::Name>([&](rb::Entity e, rb::Name& n) {
        if (!found.valid() && n.value == name) {
            found = e;
        }
    });
    return found;
}

} // namespace

static void registryExposesBuiltins() {
    const rb::ComponentRegistry registry = makeRegistry();
    CHECK(registry.entries().size() == 20u);
    CHECK(registry.find("Transform") != nullptr);
    CHECK(registry.find("SkyboxComponent") != nullptr);
    CHECK(registry.find("Camera") != nullptr);
    CHECK(registry.find("Primitive") != nullptr);
    CHECK(registry.find("Missing") == nullptr);
}

static void roundTripPreservesComponents() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    spawnHero(source);
    const rb::Entity sun = source.create();
    source.add<rb::DirectionalLight>(sun,
                                     rb::DirectionalLight{{0.0f, -1.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 3.0f});

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);

    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    CHECK(loaded.aliveCount() == 2u);
    CHECK(loaded.count<rb::Transform>() == 1u);
    CHECK(loaded.count<rb::Name>() == 1u);
    CHECK(loaded.count<rb::DirectionalLight>() == 1u);

    bool foundHero = false;
    loaded.each<rb::Transform>([&](rb::Entity, rb::Transform& t) {
        foundHero = true;
        CHECK(t.position.x == 1.0f);
        CHECK(t.position.y == 2.0f);
        CHECK(t.position.z == 3.0f);
        CHECK(t.scale.x == 2.0f);
    });
    CHECK(foundHero);
}

static void saveLoadSaveIsIdempotent() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    spawnHero(source);
    const rb::Entity lamp = source.create();
    source.add<rb::PointLight>(lamp, rb::PointLight{});
    const rb::Entity eye = source.create();
    source.add<rb::Camera>(eye, rb::Camera{});

    const std::string first = rb::SceneSerializer::toJson(source, registry).dump(2);

    rb::Scene loaded;
    rb::SceneSerializer::fromJson(nlohmann::json::parse(first), loaded, registry);
    const std::string second = rb::SceneSerializer::toJson(loaded, registry).dump(2);

    CHECK(first == second);
}

// Loading into a previously used scene must keep the file's record order (clear() frees
// indices so creation ascends again); before this held, every open-save cycle in a
// long-lived session reversed the whole file even with zero edits.
static void reloadIntoUsedSceneKeepsOrder() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    for (const char* name : {"alpha", "beta", "gamma"}) {
        const rb::Entity e = source.create();
        source.add<rb::Name>(e, rb::Name{name});
    }
    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);

    rb::Scene used;
    for (int i = 0; i < 3; ++i) {
        used.destroy(used.create()); // churn versions and the free list first
    }
    used.clear(); // the loadFromFile shape: clear, then append
    rb::SceneSerializer::fromJson(doc, used, registry);

    const nlohmann::json again = rb::SceneSerializer::toJson(used, registry);
    CHECK(again == doc);
    CHECK(again.at("entities").at(0).at("components").at("Name").at("value") == "alpha");
    CHECK(again.at("entities").at(2).at("components").at("Name").at("value") == "gamma");
}

static void malformedDataLoadsGracefully() {
    const rb::ComponentRegistry registry = makeRegistry();
    const nlohmann::json doc = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [
        { "id": 0, "components": { "Transform": { "position": [1.0, 2.0, 3.0] } } },
        { "id": 1, "components": { "Bogus": { "x": 1 } } },
        { "id": 2 },
        { "id": 3, "components": { "ModelRenderer": { "model": "not-a-uuid" } } }
      ]
    })");

    rb::Scene scene;
    rb::SceneSerializer::fromJson(doc, scene, registry); // must not throw
    CHECK(scene.aliveCount() == 4u);               // every record still became an entity
    CHECK(scene.count<rb::Transform>() == 0u);     // partial Transform skipped, not crashed
    CHECK(scene.count<rb::ModelRenderer>() == 0u); // malformed model uuid skipped
}

static void loadFromFileReplaces() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    spawnHero(source);
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_replace.scene.json";
    CHECK(rb::SceneSerializer::saveToFile(source, registry, path));

    rb::Scene target;
    spawnHero(target);
    spawnHero(target);
    CHECK(target.aliveCount() == 2u);

    CHECK(rb::SceneSerializer::loadFromFile(target, registry, path));
    CHECK(target.aliveCount() == 1u); // replaced, not appended
    CHECK(target.count<rb::Transform>() == 1u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// Valid JSON that is not a scene document ("{}", an array, a stub) must refuse without
// clearing: parse success alone once wiped the target and reported a successful load.
static void loadFromFileRefusesShapelessJson() {
    const rb::ComponentRegistry registry = makeRegistry();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_shapeless.scene.json";

    rb::Scene target;
    spawnHero(target);
    for (const char* text : {"{}", "[]", "null", "{\"entities\": {}}"}) {
        {
            std::ofstream out(path);
            out << text << '\n';
        }
        CHECK(!rb::SceneSerializer::loadFromFile(target, registry, path));
        CHECK(target.aliveCount() == 1u); // untouched
    }

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

static void saveToFileReplacesTheExistingFile() {
    const rb::ComponentRegistry registry = makeRegistry();
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_atomic_replace.scene.json";

    rb::Scene small;
    spawnHero(small);
    CHECK(rb::SceneSerializer::saveToFile(small, registry, path));

    rb::Scene bigger;
    spawnHero(bigger);
    spawnNamed(bigger, "extra");
    CHECK(rb::SceneSerializer::saveToFile(bigger, registry, path));

    rb::Scene loaded;
    CHECK(rb::SceneSerializer::loadFromFile(loaded, registry, path));
    CHECK(loaded.aliveCount() == 2u);

    std::filesystem::path temp = path;
    temp += ".tmp";
    CHECK(!std::filesystem::exists(temp)); // the working copy never outlives the save

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// A save that cannot land must refuse and leave whatever holds the target path untouched:
// a directory squatting on the name stands in for any failure at replace time, and a file
// the user write-protected must not be bypassed by the rename path.
static void saveToFileRefusalLeavesTheTargetIntact() {
    const rb::ComponentRegistry registry = makeRegistry();
    std::error_code ec;
    rb::Scene scene;
    spawnHero(scene);

    const std::filesystem::path taken =
        std::filesystem::temp_directory_path() / "rabbet_atomic_taken";
    std::filesystem::remove_all(taken, ec);
    const std::filesystem::path squatted = taken / "taken.scene.json";
    std::filesystem::create_directories(squatted, ec);
    {
        std::ofstream marker(squatted / "marker.txt");
        marker << "keep\n";
    }
    CHECK(!rb::SceneSerializer::saveToFile(scene, registry, squatted));
    CHECK(std::filesystem::is_directory(squatted));
    CHECK(std::filesystem::exists(squatted / "marker.txt"));
    std::filesystem::path temp = squatted;
    temp += ".tmp";
    CHECK(!std::filesystem::exists(temp)); // the working copy never outlives the refusal
    std::filesystem::remove_all(taken, ec);

    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "rabbet_atomic_protected";
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path target = dir / "keep.scene.json";
    CHECK(rb::SceneSerializer::saveToFile(scene, registry, target));
    const auto sizeBefore = std::filesystem::file_size(target, ec);
    std::filesystem::permissions(target,
                                 std::filesystem::perms::owner_read |
                                     std::filesystem::perms::group_read |
                                     std::filesystem::perms::others_read,
                                 std::filesystem::perm_options::replace, ec);
    CHECK(!ec);

    rb::Scene bigger;
    spawnHero(bigger);
    spawnNamed(bigger, "extra");
    CHECK(!rb::SceneSerializer::saveToFile(bigger, registry, target));

    std::filesystem::permissions(target, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, ec);
    CHECK(std::filesystem::file_size(target, ec) == sizeBefore);
    std::filesystem::remove_all(dir, ec);
}

static void saveToFileWritesThroughASymlinkedTarget() {
    const rb::ComponentRegistry registry = makeRegistry();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "rabbet_atomic_symlink";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path real = dir / "real.scene.json";
    const std::filesystem::path link = dir / "link.scene.json";

    rb::Scene scene;
    spawnHero(scene);
    CHECK(rb::SceneSerializer::saveToFile(scene, registry, real));
    std::filesystem::create_symlink(real, link, ec);
    CHECK(!ec);

    rb::Scene bigger;
    spawnHero(bigger);
    spawnNamed(bigger, "extra");
    CHECK(rb::SceneSerializer::saveToFile(bigger, registry, link));

    // The referent took the write and the link survived; nobody reading the real
    // path is left on stale bytes.
    CHECK(std::filesystem::is_symlink(std::filesystem::symlink_status(link, ec)));
    rb::Scene loaded;
    CHECK(rb::SceneSerializer::loadFromFile(loaded, registry, real));
    CHECK(loaded.aliveCount() == 2u);

    std::filesystem::remove_all(dir, ec);
}

static void saveToFileLeavesTheTargetWhenItCannotWrite() {
    const rb::ComponentRegistry registry = makeRegistry();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "rabbet_atomic_readonly";
    std::error_code ec;
    // Heal a leftover from a run killed inside the read-only window before removing.
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, ec);
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path target = dir / "keep.scene.json";

    rb::Scene scene;
    spawnHero(scene);
    CHECK(rb::SceneSerializer::saveToFile(scene, registry, target));
    const auto sizeBefore = std::filesystem::file_size(target, ec);
    CHECK(!ec);

    // With the directory read-only the temp cannot even be created, so the old file
    // survives untouched. Root ignores permission bits and fails this loudly, which
    // beats asserting nothing.
    std::filesystem::permissions(
        dir, std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec,
        std::filesystem::perm_options::replace, ec);
    CHECK(!ec);

    rb::Scene bigger;
    spawnHero(bigger);
    spawnNamed(bigger, "extra");
    CHECK(!rb::SceneSerializer::saveToFile(bigger, registry, target));

    std::filesystem::permissions(dir, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, ec);
    CHECK(std::filesystem::file_size(target, ec) == sizeBefore);

    std::filesystem::remove_all(dir, ec);
}

static void parentLinksRoundTrip() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity root = spawnNamed(source, "root");
    const rb::Entity child = spawnNamed(source, "child");
    const rb::Entity grand = spawnNamed(source, "grand");
    spawnNamed(source, "loner");
    CHECK(rb::setParent(source, child, root));
    CHECK(rb::setParent(source, grand, child));

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);

    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);
    CHECK(loaded.aliveCount() == 4u);

    const rb::Entity lroot = firstNamed(loaded, "root");
    const rb::Entity lchild = firstNamed(loaded, "child");
    const rb::Entity lgrand = firstNamed(loaded, "grand");
    const rb::Entity lloner = firstNamed(loaded, "loner");
    CHECK(rb::parentOf(loaded, lchild) == lroot);
    CHECK(rb::parentOf(loaded, lgrand) == lchild);
    CHECK(rb::parentOf(loaded, lroot) == rb::kNullEntity);
    CHECK(rb::parentOf(loaded, lloner) == rb::kNullEntity);
}

// Re-saving a loaded parented scene must reproduce the document: if parent-id emission
// ever became index-order dependent, an open-then-save with zero edits would silently
// rewire the saved hierarchy.
static void parentedSaveLoadSaveIsIdempotent() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity root = spawnHero(source);
    const rb::Entity child = spawnNamed(source, "shield");
    CHECK(rb::setParent(source, child, root));

    const std::string first = rb::SceneSerializer::toJson(source, registry).dump(2);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(nlohmann::json::parse(first), loaded, registry);
    const std::string second = rb::SceneSerializer::toJson(loaded, registry).dump(2);
    CHECK(first == second);
}

// Parent links resolve whatever the record order, and hostile links (bad type, dangling
// id, self, an authored cycle) degrade instead of crashing or looping.
static void parentRecordsResolveDefensively() {
    const rb::ComponentRegistry registry = makeRegistry();
    const nlohmann::json doc = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [
        { "id": 0, "parent": 1, "components": { "Name": { "value": "child" } } },
        { "id": 1, "components": { "Name": { "value": "root" } } },
        { "id": 2, "parent": "bogus", "components": { "Name": { "value": "a" } } },
        { "id": 3, "parent": 99, "components": { "Name": { "value": "b" } } },
        { "id": 4, "parent": 4, "components": { "Name": { "value": "c" } } },
        { "id": 5, "parent": 6, "components": { "Name": { "value": "d" } } },
        { "id": 6, "parent": 5, "components": { "Name": { "value": "e" } } }
      ]
    })");

    rb::Scene scene;
    rb::SceneSerializer::fromJson(doc, scene, registry); // must not throw
    CHECK(scene.aliveCount() == 7u);
    CHECK(rb::parentOf(scene, firstNamed(scene, "child")) == firstNamed(scene, "root"));
    CHECK(rb::parentOf(scene, firstNamed(scene, "a")) == rb::kNullEntity);
    CHECK(rb::parentOf(scene, firstNamed(scene, "b")) == rb::kNullEntity);
    CHECK(rb::parentOf(scene, firstNamed(scene, "c")) == rb::kNullEntity);

    // The authored cycle degrades to a single accepted link, never a loop.
    const bool dUnderE = rb::parentOf(scene, firstNamed(scene, "d")) == firstNamed(scene, "e");
    const bool eUnderD = rb::parentOf(scene, firstNamed(scene, "e")) == firstNamed(scene, "d");
    CHECK(dUnderE != eUnderD);
}

// Water round-trips every authored field; a document missing fields keeps their defaults, and a
// non-finite value is sanitized at load so saving it again cannot destroy the component.
static void waterRoundTrips() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity e = source.create();
    rb::WaterComponent authored;
    authored.enabled = false;
    authored.extent = glm::vec2(12.5f, 40.0f);
    authored.deepColor = glm::vec4(0.01f, 0.02f, 0.03f, 0.9f);
    authored.shallowColor = glm::vec4(0.4f, 0.5f, 0.6f, 0.3f);
    authored.waveTileScale = 0.77f;
    authored.waveStrength = 1.25f;
    authored.waveSpeed = 2.5f;
    authored.smoothness = 0.42f;
    source.add<rb::WaterComponent>(e, authored);

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);
    CHECK(loaded.count<rb::WaterComponent>() == 1u);
    loaded.each<rb::WaterComponent>([&](rb::Entity, rb::WaterComponent& w) {
        CHECK(w.enabled == false);
        CHECK(approx(w.extent.x, 12.5f));
        CHECK(approx(w.extent.y, 40.0f));
        CHECK(approx(w.deepColor.a, 0.9f));
        CHECK(approx(w.shallowColor.r, 0.4f));
        CHECK(approx(w.waveTileScale, 0.77f));
        CHECK(approx(w.waveStrength, 1.25f));
        CHECK(approx(w.waveSpeed, 2.5f));
        CHECK(approx(w.smoothness, 0.42f));
    });

    // Only the named field is overridden; everything absent keeps its default rather than
    // zeroing, so a hand-written one-field document stays a usable water plane.
    const rb::WaterComponent partial =
        nlohmann::json::parse(R"({"waveSpeed": 3.0})").get<rb::WaterComponent>();
    CHECK(partial.enabled == true);
    CHECK(approx(partial.waveSpeed, 3.0f));
    CHECK(approx(partial.extent.x, 30.0f));

    rb::Scene tolerant;
    const nlohmann::json empty = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [ { "id": 0, "components": { "WaterComponent": {} } } ]
    })");
    rb::SceneSerializer::fromJson(empty, tolerant, registry); // must not throw
    CHECK(tolerant.count<rb::WaterComponent>() == 1u);
    tolerant.each<rb::WaterComponent>([&](rb::Entity, rb::WaterComponent& w) {
        CHECK(w.enabled == true); // an empty object is defaults, not zeroes
        CHECK(approx(w.extent.x, 30.0f));
    });

    // A hand-edited magnitude that overflows to inf is clamped at load, so the document it saves
    // is loadable again. Before the guard this round-tripped as JSON null and dropped the
    // component entirely on the next load.
    const rb::WaterComponent huge =
        nlohmann::json::parse(R"({"extent": [1e40, 30.0], "waveTileScale": 1e39})")
            .get<rb::WaterComponent>();
    CHECK(std::isfinite(huge.extent.x));
    CHECK(std::isfinite(huge.waveTileScale));
    rb::Scene reload;
    rb::Scene wide;
    wide.add<rb::WaterComponent>(wide.create(), huge);
    rb::SceneSerializer::fromJson(
        nlohmann::json::parse(rb::SceneSerializer::toJson(wide, registry).dump()), reload,
        registry);
    CHECK(reload.count<rb::WaterComponent>() == 1u); // survives a real text round trip

    // A field of the wrong JSON type is NOT tolerated: parsing throws inside the registry hook,
    // the serializer logs and skips that component, and the rest of the entity still loads.
    rb::Scene mistyped;
    const nlohmann::json bad = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [ { "id": 0, "components": { "Name": { "value": "lake" },
                                               "WaterComponent": { "enabled": 1 } } } ]
    })");
    rb::SceneSerializer::fromJson(bad, mistyped, registry); // must not throw
    CHECK(mistyped.count<rb::Name>() == 1u);
    CHECK(mistyped.count<rb::WaterComponent>() == 0u);
}

// A scene carries its own sky as six texture refs. Order is load-bearing (it is GL cubemap
// face order), so the round trip has to preserve it exactly. A short list or a non-string
// entry degrades to unset faces; a malformed uuid STRING throws inside the registry hook,
// which drops the whole component the way every other asset-referencing component does.
static void skyboxFacesRoundTrip() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    const rb::Entity sky = source.create();
    rb::SkyboxComponent authored;
    for (std::size_t i = 0; i < authored.faces.size(); ++i) {
        authored.faces[i] = rb::Uuid::fromString(std::string(31, '0') + char('1' + i));
    }
    source.add<rb::SkyboxComponent>(sky, authored);

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    const rb::SkyboxComponent* active = rb::activeSkybox(loaded);
    CHECK(active != nullptr);
    CHECK(active != nullptr && active->faces == authored.faces); // same faces, same order

    rb::Scene tolerant;
    const nlohmann::json partial = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [
        { "id": 0, "components": { "SkyboxComponent": { "faces": ["00000000000000000000000000000001", 7] } } },
        { "id": 1, "components": { "SkyboxComponent": {} } }
      ]
    })");
    rb::SceneSerializer::fromJson(partial, tolerant, registry); // must not throw
    CHECK(tolerant.aliveCount() == 2u);
    const rb::SkyboxComponent* first = rb::activeSkybox(tolerant);
    CHECK(first != nullptr);
    CHECK(first != nullptr && first->faces[0].valid());
    CHECK(first != nullptr && !first->faces[1].valid()); // the non-string entry stayed unset
    CHECK(first != nullptr && !first->faces[5].valid()); // and the missing tail too

    // A malformed uuid string is not tolerated field-by-field: parseUuid throws, the
    // serializer logs and skips the component, and the entity loads without a sky.
    rb::Scene malformed;
    const nlohmann::json bad = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [
        { "id": 0, "components": { "Name": { "value": "sky" },
          "SkyboxComponent": { "faces": ["not-a-uuid", "00000000000000000000000000000002",
                                          "00000000000000000000000000000003",
                                          "00000000000000000000000000000004",
                                          "00000000000000000000000000000005",
                                          "00000000000000000000000000000006"] } } }
      ]
    })");
    rb::SceneSerializer::fromJson(bad, malformed, registry); // must not throw
    CHECK(malformed.aliveCount() == 1u);
    CHECK(rb::activeSkybox(malformed) == nullptr); // dropped, not half-applied
}

static void staleParentIsNotSaved() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene scene;
    const rb::Entity parent = spawnNamed(scene, "gone");
    const rb::Entity child = spawnNamed(scene, "kept");
    CHECK(rb::setParent(scene, child, parent));
    scene.destroy(parent);

    const nlohmann::json doc = rb::SceneSerializer::toJson(scene, registry);
    CHECK(doc["entities"].size() == 1u);
    CHECK(!doc["entities"][0].contains("parent"));
}

// A component whose asset ref was never assigned (a fresh emitter, a noise terrain) must
// survive save -> load -> save; the null uuid used to serialize as 32 zeros and the loader
// rejected that as malformed, silently dropping the component.
static void unsetAssetRefsSurviveRoundTrip() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity e = spawnNamed(scene, "bare");
    scene.add<rb::ModelRenderer>(e, rb::ModelRenderer{});
    scene.add<rb::ParticleEmitter>(e, rb::ParticleEmitter{});
    scene.add<rb::TerrainComponent>(e, rb::TerrainComponent{});

    const nlohmann::json first = rb::SceneSerializer::toJson(scene, registry);
    CHECK(first["entities"][0]["components"]["ModelRenderer"]["model"] == "");

    rb::Scene loaded;
    rb::SceneSerializer::fromJson(first, loaded, registry);
    const rb::Entity l = firstNamed(loaded, "bare");
    CHECK(l.valid());
    CHECK(loaded.has<rb::ModelRenderer>(l));
    CHECK(loaded.has<rb::ParticleEmitter>(l));
    CHECK(loaded.has<rb::TerrainComponent>(l));
    CHECK(!loaded.get<rb::ModelRenderer>(l).model.valid());

    const nlohmann::json second = rb::SceneSerializer::toJson(loaded, registry);
    CHECK(first == second);
}

// Scenes saved before the "" convention carry the null uuid spelled out; those must load as
// unset, while genuinely malformed text still drops the component.
static void zeroUuidTextLoadsAsUnset() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity e = spawnNamed(scene, "legacy");
    scene.add<rb::ModelRenderer>(e, rb::ModelRenderer{});

    nlohmann::json doc = rb::SceneSerializer::toJson(scene, registry);
    doc["entities"][0]["components"]["ModelRenderer"]["model"] = std::string(32, '0');
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);
    const rb::Entity l = firstNamed(loaded, "legacy");
    CHECK(loaded.has<rb::ModelRenderer>(l));
    CHECK(!loaded.get<rb::ModelRenderer>(l).model.valid());

    doc["entities"][0]["components"]["ModelRenderer"]["model"] = "not-a-uuid";
    rb::Scene rejected;
    rb::SceneSerializer::fromJson(doc, rejected, registry);
    const rb::Entity r = firstNamed(rejected, "legacy");
    CHECK(r.valid());
    CHECK(!rejected.has<rb::ModelRenderer>(r));

    // A truncated zero run is corruption, not the canonical null spelling.
    doc["entities"][0]["components"]["ModelRenderer"]["model"] = "0";
    rb::Scene truncated;
    rb::SceneSerializer::fromJson(doc, truncated, registry);
    CHECK(!truncated.has<rb::ModelRenderer>(firstNamed(truncated, "legacy")));
}

void serializeSceneSuite() {
    registryExposesBuiltins();
    roundTripPreservesComponents();
    saveLoadSaveIsIdempotent();
    reloadIntoUsedSceneKeepsOrder();
    malformedDataLoadsGracefully();
    loadFromFileReplaces();
    loadFromFileRefusesShapelessJson();
    saveToFileReplacesTheExistingFile();
    saveToFileRefusalLeavesTheTargetIntact();
    saveToFileWritesThroughASymlinkedTarget();
    saveToFileLeavesTheTargetWhenItCannotWrite();
    parentLinksRoundTrip();
    parentedSaveLoadSaveIsIdempotent();
    parentRecordsResolveDefensively();
    waterRoundTrips();
    skyboxFacesRoundTrip();
    staleParentIsNotSaved();
    unsetAssetRefsSurviveRoundTrip();
    zeroUuidTextLoadsAsUnset();
}

namespace {

} // namespace

static void duplicateCopiesComponents() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity src = scene.create();
    scene.add<rb::Name>(src, rb::Name{"box"});
    rb::Transform t;
    t.position = {1.0f, 2.0f, 3.0f};
    t.scale = {2.0f, 2.0f, 2.0f};
    scene.add<rb::Transform>(src, t);
    scene.add<rb::Primitive>(
        src, rb::Primitive{rb::PrimitiveShape::Sphere, {0.2f, 0.4f, 0.6f}, 0.5f, 0.3f});

    const rb::Entity copy = rb::SceneSerializer::duplicateEntity(scene, registry, src);

    CHECK(copy != src);
    CHECK(scene.alive(copy));
    CHECK(scene.aliveCount() == 2u);
    CHECK(scene.has<rb::Name>(copy));
    CHECK(scene.has<rb::Transform>(copy));
    CHECK(scene.has<rb::Primitive>(copy));

    CHECK(scene.get<rb::Name>(copy).value == "box");
    const rb::Transform& ct = scene.get<rb::Transform>(copy);
    CHECK(ct.position.x == 1.0f);
    CHECK(ct.position.z == 3.0f);
    CHECK(ct.scale.x == 2.0f);

    const rb::Primitive& cp = scene.get<rb::Primitive>(copy);
    CHECK(cp.shape == rb::PrimitiveShape::Sphere);
    CHECK(cp.color.y == 0.4f);
    CHECK(cp.metallic == 0.5f);
    CHECK(cp.roughness == 0.3f);
}

static void duplicateIsIndependent() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity src = scene.create();
    rb::Transform t;
    t.position = {5.0f, 0.0f, 0.0f};
    scene.add<rb::Transform>(src, t);

    const rb::Entity copy = rb::SceneSerializer::duplicateEntity(scene, registry, src);
    scene.get<rb::Transform>(copy).position.x = 99.0f;

    CHECK(scene.get<rb::Transform>(src).position.x == 5.0f);
    CHECK(scene.get<rb::Transform>(copy).position.x == 99.0f);
}

static void duplicateResetsModelHandle() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;
    const rb::Entity src = scene.create();
    rb::ModelRenderer renderer;
    renderer.model = rb::Uuid::generate();
    renderer.handle = rb::AssetHandle<rb::ModelAsset>{7u, 3u};
    CHECK(renderer.handle.valid());
    scene.add<rb::ModelRenderer>(src, renderer);

    const rb::Entity copy = rb::SceneSerializer::duplicateEntity(scene, registry, src);
    const rb::ModelRenderer& cr = scene.get<rb::ModelRenderer>(copy);
    CHECK(cr.model == renderer.model); // stable uuid copied
    CHECK(!cr.handle.valid());         // runtime handle reset for re-resolution
}

static void duplicateSubtreeMirrorsTheTree() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity root = scene.create();
    scene.add<rb::Name>(root, rb::Name{"root"});
    const rb::Entity child = scene.create();
    scene.add<rb::Name>(child, rb::Name{"child"});
    const rb::Entity grand = scene.create();
    scene.add<rb::Name>(grand, rb::Name{"grand"});
    CHECK(rb::setParent(scene, child, root));
    CHECK(rb::setParent(scene, grand, child));

    const rb::Entity copy = rb::SceneSerializer::duplicateSubtree(scene, registry, root);

    CHECK(scene.aliveCount() == 6u);
    CHECK(copy != root);
    CHECK(rb::parentOf(scene, copy) == rb::kNullEntity);

    const std::vector<rb::Entity> copiedChildren = rb::childrenOf(scene, copy);
    CHECK(copiedChildren.size() == 1u);
    if (!copiedChildren.empty()) {
        const rb::Entity copiedChild = copiedChildren.front();
        CHECK(copiedChild != child); // linked to the copy, not shared with the original
        CHECK(scene.get<rb::Name>(copiedChild).value == "child");
        const std::vector<rb::Entity> copiedGrand = rb::childrenOf(scene, copiedChild);
        CHECK(copiedGrand.size() == 1u);
        CHECK(!copiedGrand.empty() && scene.get<rb::Name>(copiedGrand.front()).value == "grand");
    }

    // The original tree is untouched.
    CHECK(rb::childrenOf(scene, root).size() == 1u);
    CHECK(rb::parentOf(scene, child) == root);
    CHECK(rb::parentOf(scene, grand) == child);
}

static void duplicateSubtreeKeepsTheRootParent() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity base = scene.create();
    const rb::Entity source = scene.create();
    const rb::Entity leaf = scene.create();
    CHECK(rb::setParent(scene, source, base));
    CHECK(rb::setParent(scene, leaf, source));

    const rb::Entity copy = rb::SceneSerializer::duplicateSubtree(scene, registry, source);

    CHECK(rb::parentOf(scene, copy) == base); // the copy stays a sibling of the source
    CHECK(rb::childrenOf(scene, base).size() == 2u);
    CHECK(rb::childrenOf(scene, copy).size() == 1u);
}

// A dead source duplicates to nothing: no zombie copy is minted for either entry point.
static void duplicateDeadSourceCreatesNothing() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity doomed = scene.create();
    scene.add<rb::Name>(doomed, rb::Name{"gone"});
    scene.destroy(doomed);
    const std::size_t before = scene.aliveCount();

    CHECK(!scene.alive(rb::SceneSerializer::duplicateEntity(scene, registry, doomed)));
    CHECK(scene.aliveCount() == before);
    CHECK(!scene.alive(rb::SceneSerializer::duplicateSubtree(scene, registry, doomed)));
    CHECK(scene.aliveCount() == before);
}

// Corrupt cyclic links (authored around the gate) duplicate each member once instead of
// spinning the collector forever.
static void duplicateSubtreeOfCorruptCycleTerminates() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene scene;

    const rb::Entity a = scene.create();
    scene.add<rb::Name>(a, rb::Name{"a"});
    const rb::Entity b = scene.create();
    scene.add<rb::Name>(b, rb::Name{"b"});
    scene.add<rb::Parent>(a, rb::Parent{b});
    scene.add<rb::Parent>(b, rb::Parent{a});

    const rb::Entity copy = rb::SceneSerializer::duplicateSubtree(scene, registry, a);

    CHECK(scene.alive(copy));
    CHECK(scene.aliveCount() == 4u); // two originals + exactly two copies
}

void serializeDuplicateSuite() {
    duplicateCopiesComponents();
    duplicateIsIndependent();
    duplicateResetsModelHandle();
    duplicateSubtreeMirrorsTheTree();
    duplicateSubtreeKeepsTheRootParent();
    duplicateDeadSourceCreatesNothing();
    duplicateSubtreeOfCorruptCycleTerminates();
}

namespace {

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

// Reverting a subtree instance rebuilds the whole tree from the asset: local overrides and
// components added after instantiation are discarded, a destroyed prefab child comes back,
// an entity parented in after instantiation goes, and the link stays on the root.
static void revertRebuildsSubtree() {
    const rb::ComponentRegistry registry = makeRegistry();
    rb::Scene authoring;
    const rb::PrefabAsset prefab = prefabFromEntity(authoring, registry, spawnLampPost(authoring));

    rb::Scene scene;
    const rb::Entity root = rb::instantiatePrefab(scene, registry, prefab);
    scene.add<rb::PrefabInstance>(root, rb::PrefabInstance{rb::Uuid::generate(), {}});

    scene.get<rb::Transform>(root).position.x = 99.0f;
    scene.add<rb::Camera>(root, rb::Camera{});
    const rb::Entity head = childNamed(scene, root, "Head");
    rb::destroySubtree(scene, childNamed(scene, head, "Glow"));
    const rb::Entity extra = scene.create();
    scene.add<rb::Name>(extra, rb::Name{"Extra"});
    CHECK(rb::setParent(scene, extra, head));

    rb::applyPrefab(scene, registry, root, prefab);

    CHECK(scene.alive(root));
    CHECK(scene.has<rb::PrefabInstance>(root));
    CHECK(scene.get<rb::Transform>(root).position.x == 5.0f); // override reverted
    CHECK(!scene.has<rb::Camera>(root)); // the post-instantiation addition went with it
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
// new prefab never silently overwrites an existing file; it de-collides with _1, _2, ...
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

void prefabSuite() {
    entityToPrefabExcludesInstanceLink();
    captureWritesSubtreeRecords();
    instantiateRebuildsTree();
    revertRebuildsSubtree();
    revertAgainstEmptyAssetIsANoOp();
    revertKeepsScenePlacement();
    oldSingleEntityShapeLoads();
    savedFileRoundTrips();
    resolveLinksInstance();
    instanceSceneRoundTrip();
    prefabFilenameSafety();
    sampleLampPostInstantiates();
}

int main() {
    serializeRegistrySuite();
    serializeSceneSuite();
    serializeDuplicateSuite();
    prefabSuite();
    return rbtest::summary("serialize");
}

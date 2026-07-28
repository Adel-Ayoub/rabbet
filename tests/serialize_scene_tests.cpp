#include "rabbet/ecs/Scene.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

namespace {

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

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
    CHECK(registry.entries().size() == 19u);
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

static void saveAndLoadFile() {
    const rb::ComponentRegistry registry = makeRegistry();

    rb::Scene source;
    spawnHero(source);

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_scene_roundtrip.scene.json";

    CHECK(rb::SceneSerializer::saveToFile(source, registry, path));

    rb::Scene loaded;
    CHECK(rb::SceneSerializer::loadFromFile(loaded, registry, path));
    CHECK(loaded.count<rb::Transform>() == 1u);
    CHECK(loaded.count<rb::Name>() == 1u);

    std::error_code ec;
    std::filesystem::remove(path, ec);
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

static void saveToFileSurvivesAFailedReplace() {
    const rb::ComponentRegistry registry = makeRegistry();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "rabbet_atomic_taken";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    // A directory squatting on the target path stands in for any failure at replace
    // time: the save must refuse, clean up its temp and leave the squatter alone.
    const std::filesystem::path target = dir / "taken.scene.json";
    std::filesystem::create_directories(target, ec);
    {
        std::ofstream marker(target / "marker.txt");
        marker << "keep\n";
    }

    rb::Scene scene;
    spawnHero(scene);
    CHECK(!rb::SceneSerializer::saveToFile(scene, registry, target));
    CHECK(std::filesystem::is_directory(target));
    CHECK(std::filesystem::exists(target / "marker.txt"));
    std::filesystem::path temp = target;
    temp += ".tmp";
    CHECK(!std::filesystem::exists(temp));

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

static void saveToFileRefusesAWriteProtectedTarget() {
    const rb::ComponentRegistry registry = makeRegistry();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "rabbet_atomic_protected";
    std::error_code ec;
    std::filesystem::permissions(dir, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, ec);
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path target = dir / "keep.scene.json";

    rb::Scene scene;
    spawnHero(scene);
    CHECK(rb::SceneSerializer::saveToFile(scene, registry, target));
    const auto sizeBefore = std::filesystem::file_size(target, ec);

    // The rename path would bypass the file's own write protection, so the save has
    // to refuse it the way the direct write used to.
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

static void saveToFileReclaimsAPlantedTempLink() {
    const rb::ComponentRegistry registry = makeRegistry();
    const std::filesystem::path dir =
        std::filesystem::temp_directory_path() / "rabbet_atomic_planted";
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path target = dir / "keep.scene.json";

    rb::Scene scene;
    spawnHero(scene);
    CHECK(rb::SceneSerializer::saveToFile(scene, registry, target));

    // A temp-name symlink pointing at the target would route the write into the
    // target and then rename the link over it, destroying both copies at once.
    std::filesystem::path temp = target;
    temp += ".tmp";
    std::filesystem::create_symlink(target, temp, ec);
    CHECK(!ec);

    rb::Scene bigger;
    spawnHero(bigger);
    spawnNamed(bigger, "extra");
    CHECK(rb::SceneSerializer::saveToFile(bigger, registry, target));

    CHECK(std::filesystem::is_regular_file(target));
    CHECK(!std::filesystem::exists(std::filesystem::symlink_status(temp, ec)));
    rb::Scene loaded;
    CHECK(rb::SceneSerializer::loadFromFile(loaded, registry, target));
    CHECK(loaded.aliveCount() == 2u);

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

static void childRecordBeforeParentRecordResolves() {
    const rb::ComponentRegistry registry = makeRegistry();
    const nlohmann::json doc = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [
        { "id": 0, "parent": 1, "components": { "Name": { "value": "child" } } },
        { "id": 1, "components": { "Name": { "value": "root" } } }
      ]
    })");

    rb::Scene scene;
    rb::SceneSerializer::fromJson(doc, scene, registry);
    const rb::Entity child = firstNamed(scene, "child");
    const rb::Entity root = firstNamed(scene, "root");
    CHECK(rb::parentOf(scene, child) == root);
}

static void malformedParentIsSkipped() {
    const rb::ComponentRegistry registry = makeRegistry();
    const nlohmann::json doc = nlohmann::json::parse(R"({
      "version": 1,
      "entities": [
        { "id": 0, "parent": "bogus", "components": { "Name": { "value": "a" } } },
        { "id": 1, "parent": 99, "components": { "Name": { "value": "b" } } },
        { "id": 2, "parent": 2, "components": { "Name": { "value": "c" } } },
        { "id": 3, "parent": 4, "components": { "Name": { "value": "d" } } },
        { "id": 4, "parent": 3, "components": { "Name": { "value": "e" } } }
      ]
    })");

    rb::Scene scene;
    rb::SceneSerializer::fromJson(doc, scene, registry); // must not throw
    CHECK(scene.aliveCount() == 5u);
    CHECK(rb::parentOf(scene, firstNamed(scene, "a")) == rb::kNullEntity);
    CHECK(rb::parentOf(scene, firstNamed(scene, "b")) == rb::kNullEntity);
    CHECK(rb::parentOf(scene, firstNamed(scene, "c")) == rb::kNullEntity);

    // The authored cycle degrades to a single accepted link, never a loop.
    const bool dUnderE = rb::parentOf(scene, firstNamed(scene, "d")) == firstNamed(scene, "e");
    const bool eUnderD = rb::parentOf(scene, firstNamed(scene, "e")) == firstNamed(scene, "d");
    CHECK(dUnderE != eUnderD);
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

int main() {
    registryExposesBuiltins();
    roundTripPreservesComponents();
    saveLoadSaveIsIdempotent();
    reloadIntoUsedSceneKeepsOrder();
    saveAndLoadFile();
    malformedDataLoadsGracefully();
    loadFromFileReplaces();
    loadFromFileRefusesShapelessJson();
    saveToFileReplacesTheExistingFile();
    saveToFileSurvivesAFailedReplace();
    saveToFileLeavesTheTargetWhenItCannotWrite();
    saveToFileRefusesAWriteProtectedTarget();
    saveToFileWritesThroughASymlinkedTarget();
    saveToFileReclaimsAPlantedTempLink();
    parentLinksRoundTrip();
    parentedSaveLoadSaveIsIdempotent();
    childRecordBeforeParentRecordResolves();
    malformedParentIsSkipped();
    skyboxFacesRoundTrip();
    staleParentIsNotSaved();
    unsetAssetRefsSurviveRoundTrip();
    zeroUuidTextLoadsAsUnset();
    return rbtest::summary("serialize_scene");
}

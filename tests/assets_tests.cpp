#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetMeta.h"
#include "rabbet/assets/AssetTree.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/assets/ThumbnailCache.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/AssetResolveSystem.h"
#include "rabbet/render/MaterialImport.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/scene/Camera.h"
#include "rabbet/scene/Light.h"
#include "rabbet/scene/Name.h"
#include "rabbet/scripting/ScriptAssetResolveSystem.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptField.h"
#include "rabbet/scripting/ScriptSystem.h"
#include "rabbet/serialize/AssetCreate.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/Prefab.h"
#include "rabbet/serialize/SceneSerializer.h"

#include "tests/Test.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <vector>

static void uuidsGenerateValidAndUnique() {
    const rb::Uuid a = rb::Uuid::generate();
    const rb::Uuid b = rb::Uuid::generate();
    CHECK(a.valid());
    CHECK(b.valid());
    CHECK(!(a == b));
    CHECK(!rb::Uuid{}.valid());
}

static void uuidStringRoundTrips() {
    const rb::Uuid a = rb::Uuid::generate();
    const std::string text = a.toString();
    CHECK(text.size() == 32u);
    CHECK(rb::Uuid::fromString(text) == a);
    CHECK(!rb::Uuid::fromString("not-a-uuid").valid());
    CHECK(!rb::Uuid::fromString("").valid());
    CHECK(!rb::Uuid::fromString("dead").valid());
}

static void metadataPersistsAcrossRuns() {
    const std::filesystem::path asset =
        std::filesystem::temp_directory_path() / "rabbet_meta_asset.gltf";
    const std::filesystem::path sidecar = rb::assetmeta::sidecarPath(asset);
    std::error_code ec;
    std::filesystem::remove(sidecar, ec);
    { std::ofstream(asset) << "stub"; } // sidecars are only written for a real source

    const rb::assetmeta::Metadata first = rb::assetmeta::loadOrCreate(asset, rb::AssetType::Model);
    CHECK(first.id.valid());
    CHECK(first.type == rb::AssetType::Model);
    CHECK(first.name == "rabbet_meta_asset");
    CHECK(std::filesystem::exists(sidecar));

    // A second call stands in for a later run: it reads the sidecar, keeping identity.
    const rb::assetmeta::Metadata second = rb::assetmeta::loadOrCreate(asset, rb::AssetType::Model);
    CHECK(second.id == first.id);
    CHECK(second.type == rb::AssetType::Model);
    CHECK(second.name == first.name);

    std::filesystem::remove(sidecar, ec);
    std::filesystem::remove(asset, ec);
}

static void uuidHelperStillWorks() {
    const std::filesystem::path asset =
        std::filesystem::temp_directory_path() / "rabbet_meta_uuid.png";
    const std::filesystem::path sidecar = rb::assetmeta::sidecarPath(asset);
    std::error_code ec;
    std::filesystem::remove(sidecar, ec);
    { std::ofstream(asset) << "stub"; }

    const rb::Uuid a = rb::assetmeta::loadOrCreateUuid(asset);
    const rb::Uuid b = rb::assetmeta::loadOrCreateUuid(asset);
    CHECK(a.valid());
    CHECK(a == b);

    std::filesystem::remove(sidecar, ec);
    std::filesystem::remove(asset, ec);
}

static void absentSourceNeedsNoReimport() {
    const std::filesystem::path asset =
        std::filesystem::temp_directory_path() / "rabbet_meta_absent.fbx";
    const std::filesystem::path sidecar = rb::assetmeta::sidecarPath(asset);
    std::error_code ec;
    std::filesystem::remove(sidecar, ec);

    (void)rb::assetmeta::loadOrCreate(asset, rb::AssetType::Model);
    CHECK(!rb::assetmeta::needsReimport(asset)); // no source file on disk
    // And no ghost sidecar: a load retried against a deleted asset must not litter the
    // directory with fresh .import files carrying ever-changing uuids.
    CHECK(!std::filesystem::exists(sidecar));

    std::filesystem::remove(sidecar, ec);
}

static void corruptSidecarIsNotOverwritten() {
    const std::filesystem::path asset =
        std::filesystem::temp_directory_path() / "rabbet_meta_corrupt.gltf";
    const std::filesystem::path sidecar = rb::assetmeta::sidecarPath(asset);
    std::error_code ec;
    std::filesystem::remove(sidecar, ec);
    { std::ofstream(asset) << "stub"; }

    (void)rb::assetmeta::loadOrCreate(asset, rb::AssetType::Model); // writes a valid sidecar
    {
        std::ofstream out(sidecar, std::ios::trunc);
        out << "{ not valid json";
    }

    // A corrupt sidecar must be preserved, never silently reassigned.
    (void)rb::assetmeta::loadOrCreate(asset, rb::AssetType::Model);

    std::ifstream in(sidecar);
    std::string contents;
    std::getline(in, contents, '\0');
    CHECK(contents.find("not valid json") != std::string::npos);

    std::filesystem::remove(sidecar, ec);
    std::filesystem::remove(asset, ec);
}

void assetsMetaSuite() {
    uuidsGenerateValidAndUnique();
    uuidStringRoundTrips();
    metadataPersistsAcrossRuns();
    uuidHelperStillWorks();
    absentSourceNeedsNoReimport();
    corruptSidecarIsNotOverwritten();
}

namespace {

struct Thing {
    int x = 0;
};

struct Other {
    float y = 0.0f;
};

} // namespace

static void addGetErase() {
    rb::AssetManager assets;
    const rb::AssetHandle<Thing> h = assets.add<Thing>(Thing{42});
    CHECK(assets.valid<Thing>(h));
    CHECK(assets.get<Thing>(h) != nullptr);
    CHECK(assets.get<Thing>(h)->x == 42);
    CHECK(assets.count<Thing>() == 1u);

    assets.erase<Thing>(h);
    CHECK(!assets.valid<Thing>(h));
    CHECK(assets.get<Thing>(h) == nullptr);
    CHECK(assets.count<Thing>() == 0u);
}

static void reusedSlotInvalidatesOldHandle() {
    rb::AssetManager assets;
    const rb::AssetHandle<Thing> first = assets.add<Thing>(Thing{1});
    assets.erase<Thing>(first);
    const rb::AssetHandle<Thing> second = assets.add<Thing>(Thing{2});

    CHECK(second.index == first.index);
    CHECK(second.generation != first.generation);
    CHECK(assets.valid<Thing>(second));
    CHECK(!assets.valid<Thing>(first));
}

static void lookupByUuidIsTypeSafe() {
    rb::AssetManager assets;
    const rb::Uuid id = rb::Uuid::generate();
    const rb::AssetHandle<Thing> h = assets.add<Thing>(Thing{7}, id);

    CHECK(assets.uuidOf<Thing>(h) == id);
    CHECK(assets.find<Thing>(id) == h);
    CHECK(!assets.find<Other>(id).valid());
    CHECK(!assets.find<Thing>(rb::Uuid::generate()).valid());
}

static void loadCachesByPath() {
    rb::AssetManager assets;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_asset_cache.bin";
    std::error_code ec;
    std::filesystem::remove(rb::assetmeta::sidecarPath(path), ec);

    int imports = 0;
    const auto importer = [&imports](const std::filesystem::path&) -> std::optional<Thing> {
        ++imports;
        return Thing{99};
    };

    const rb::AssetHandle<Thing> a = assets.load<Thing>(path, importer);
    const rb::AssetHandle<Thing> b = assets.load<Thing>(path, importer);

    CHECK(a.valid());
    CHECK(a == b);
    CHECK(imports == 1);
    CHECK(assets.get<Thing>(a) != nullptr);
    CHECK(assets.get<Thing>(a)->x == 99);

    std::filesystem::remove(rb::assetmeta::sidecarPath(path), ec);
}

static void loadDedupsEvenWithCorruptSidecar() {
    rb::AssetManager assets;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_asset_badmeta.bin";
    const std::filesystem::path sidecar = rb::assetmeta::sidecarPath(path);
    std::error_code ec;
    std::filesystem::remove(sidecar, ec);
    {
        std::ofstream out(sidecar, std::ios::trunc);
        out << "{ broken";
    }

    int imports = 0;
    const auto importer = [&imports](const std::filesystem::path&) -> std::optional<Thing> {
        ++imports;
        return Thing{5};
    };

    const rb::AssetHandle<Thing> a = assets.load<Thing>(path, importer);
    const rb::AssetHandle<Thing> b = assets.load<Thing>(path, importer);

    CHECK(a.valid());
    CHECK(a == b);
    CHECK(imports == 1); // path cache dedups despite the unreadable sidecar

    std::filesystem::remove(sidecar, ec);
}

// Systems hold get() pointers across further loads (a resolve can lazily import
// mid-frame), so store growth must never move existing assets.
static void pointersSurviveStoreGrowth() {
    rb::AssetManager assets;
    const rb::AssetHandle<Thing> first = assets.add<Thing>(Thing{11});
    const Thing* pinned = assets.get<Thing>(first);
    CHECK(pinned != nullptr);

    for (int i = 0; i < 1000; ++i) {
        (void)assets.add<Thing>(Thing{i});
    }

    CHECK(assets.get<Thing>(first) == pinned); // same slot, same address
    CHECK(pinned->x == 11);
}

// A load that failed is not retried on the very next call: the resolve systems ask every
// frame, and one broken path must not become per-frame disk work. (The retry window later
// reopens, so a repaired file still self-heals.)
static void failedLoadIsThrottled() {
    rb::AssetManager assets;
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_asset_throttle.bin";
    const std::filesystem::path sidecar = rb::assetmeta::sidecarPath(path);
    std::error_code ec;
    std::filesystem::remove(path, ec);
    std::filesystem::remove(sidecar, ec);

    int attempts = 0;
    const auto importer = [&attempts](const std::filesystem::path&) -> std::optional<Thing> {
        ++attempts;
        return std::nullopt; // the file is unreadable
    };

    CHECK(!assets.load<Thing>(path, importer).valid());
    CHECK(!assets.load<Thing>(path, importer).valid());
    CHECK(!assets.load<Thing>(path, importer).valid());
    CHECK(attempts == 1); // the follow-up calls were absorbed by the throttle

    std::filesystem::remove(sidecar, ec);
}

void assetsManagerSuite() {
    addGetErase();
    reusedSlotInvalidatesOldHandle();
    lookupByUuidIsTypeSafe();
    loadCachesByPath();
    loadDedupsEvenWithCorruptSidecar();
    pointersSurviveStoreGrowth();
    failedLoadIsThrottled();
}

namespace {

namespace fs = std::filesystem;

void writeFile(const fs::path& path, const char* content = "x") {
    fs::create_directories(path.parent_path());
    std::ofstream out(path);
    out << content;
}

fs::path makeTempProject() {
    const fs::path root = fs::temp_directory_path() / "rabbet_assetdb_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeFile(root / "textures" / "wall.png");
    writeFile(root / "models" / "crate.obj");
    writeFile(root / "scenes" / "level.scene.json", "{}");
    writeFile(root / "notes.txt"); // not an asset
    return root;
}

int countType(const rb::AssetDatabase& db, rb::AssetType type) {
    int n = 0;
    for (const rb::AssetDatabase::Record& r : db.records()) {
        if (r.type == type) {
            ++n;
        }
    }
    return n;
}

} // namespace

static void extensionClassification() {
    CHECK(rb::assetTypeFromExtension("foo/bar.PNG") == rb::AssetType::Texture);
    CHECK(rb::assetTypeFromExtension("a.obj") == rb::AssetType::Model);
    CHECK(rb::assetTypeFromExtension("a.GLB") == rb::AssetType::Model);
    CHECK(rb::assetTypeFromExtension("world.scene.json") == rb::AssetType::Scene);
    CHECK(rb::assetTypeFromExtension("hero.prefab.json") == rb::AssetType::Prefab);
    CHECK(rb::assetTypeFromExtension("readme.txt") == rb::AssetType::Unknown);
    CHECK(rb::assetTypeFromExtension("plain.json") == rb::AssetType::Unknown);
}

static void scanCataloguesAssets() {
    const fs::path root = makeTempProject();
    rb::AssetManager manager;
    rb::AssetDatabase db;

    const std::size_t n = db.scan(root, &manager);
    CHECK(n == 3u);
    CHECK(db.size() == 3u);
    CHECK(countType(db, rb::AssetType::Texture) == 1);
    CHECK(countType(db, rb::AssetType::Model) == 1);
    CHECK(countType(db, rb::AssetType::Scene) == 1);

    for (const rb::AssetDatabase::Record& r : db.records()) {
        CHECK(r.id.valid());
        CHECK(db.find(r.id) == &r);
        CHECK(db.findByPath(r.path) != nullptr);
        CHECK(manager.sourcePath(r.id).has_value());
        CHECK(manager.sourceType(r.id) == r.type);
    }

    std::error_code ec;
    fs::remove_all(root, ec);
}

static void uuidsAreStableAcrossScans() {
    const fs::path root = makeTempProject();
    rb::AssetDatabase db;

    CHECK(db.scan(root) == 3u);
    const rb::AssetDatabase::Record* crate = db.findByPath(root / "models" / "crate.obj");
    CHECK(crate != nullptr);
    const rb::Uuid first = crate != nullptr ? crate->id : rb::Uuid{};

    // The ".import" sidecars persist, so a re-scan must reuse the same uuids.
    CHECK(db.scan(root) == 3u);
    const rb::AssetDatabase::Record* again = db.findByPath(root / "models" / "crate.obj");
    CHECK(again != nullptr);
    CHECK(again != nullptr && again->id == first);

    std::error_code ec;
    fs::remove_all(root, ec);
}

static void missingDirectoryIsEmpty() {
    rb::AssetDatabase db;
    const fs::path missing = fs::temp_directory_path() / "rabbet_assetdb_missing_xyz";
    std::error_code ec;
    fs::remove_all(missing, ec);
    CHECK(db.scan(missing) == 0u);
    CHECK(db.size() == 0u);
}

// A copy-pasted asset+sidecar pair repeats a uuid; the scan keeps the first record and
// skips the copy instead of letting the two silently alias by iteration order.
static void duplicateUuidKeepsTheFirstRecord() {
    const fs::path root = fs::temp_directory_path() / "rabbet_assetdb_dup_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    writeFile(root / "a_first.png");
    writeFile(root / "b_copy.png");
    const char* sidecar = "{\n"
                          "  \"uuid\": \"0123456789abcdef0123456789abcdef\",\n"
                          "  \"type\": \"Texture\",\n"
                          "  \"name\": \"first\",\n"
                          "  \"version\": 1\n"
                          "}\n";
    writeFile(root / "a_first.png.import", sidecar);
    writeFile(root / "b_copy.png.import", sidecar);

    rb::AssetDatabase db;
    CHECK(db.scan(root) == 1u); // whichever is seen first wins; the other is skipped
    CHECK(db.find(rb::Uuid::fromString("0123456789abcdef0123456789abcdef")) != nullptr);

    fs::remove_all(root, ec);
}

void assetsDatabaseSuite() {
    extensionClassification();
    scanCataloguesAssets();
    uuidsAreStableAcrossScans();
    missingDirectoryIsEmpty();
    duplicateUuidKeepsTheFirstRecord();
}

namespace {

rb::AssetDatabase::Record rec(std::uint64_t id, const std::string& path, rb::AssetType type,
                              const std::string& name) {
    return rb::AssetDatabase::Record{rb::Uuid{0u, id}, std::filesystem::path(path), type, name};
}

const rb::AssetTree* folder(const rb::AssetTree& node, const std::string& name) {
    for (const rb::AssetTree& f : node.folders) {
        if (f.name == name) {
            return &f;
        }
    }
    return nullptr;
}

const rb::AssetTree::Leaf* leaf(const rb::AssetTree& node, const std::string& name) {
    for (const rb::AssetTree::Leaf& a : node.assets) {
        if (a.name == name) {
            return &a;
        }
    }
    return nullptr;
}

static void emptyRecordsYieldEmptyTree() {
    const std::vector<rb::AssetDatabase::Record> records;
    const rb::AssetTree tree = rb::buildAssetTree("/proj/assets", records);
    CHECK(tree.empty());
    CHECK(tree.folders.empty());
    CHECK(tree.assets.empty());
}

static void rootLevelAssetsLiveAtTreeRoot() {
    std::vector<rb::AssetDatabase::Record> records;
    records.push_back(rec(1, "/proj/assets/readme.lua", rb::AssetType::Script, "readme"));
    records.push_back(rec(2, "/proj/assets/logo.png", rb::AssetType::Texture, "logo"));
    const rb::AssetTree tree = rb::buildAssetTree("/proj/assets", records);
    CHECK(tree.folders.empty());
    CHECK(tree.assets.size() == 2u);
    // Sorted by name (case-insensitive): "logo" before "readme".
    CHECK(tree.assets[0].name == "logo");
    CHECK(tree.assets[1].name == "readme");
}

static void nestedFoldersFromRelativePaths() {
    std::vector<rb::AssetDatabase::Record> records;
    records.push_back(rec(1, "/proj/assets/textures/grass.png", rb::AssetType::Texture, "grass"));
    records.push_back(rec(2, "/proj/assets/textures/rock.png", rb::AssetType::Texture, "rock"));
    records.push_back(rec(3, "/proj/assets/models/crate.gltf", rb::AssetType::Model, "crate"));
    const rb::AssetTree tree = rb::buildAssetTree("/proj/assets", records);

    CHECK(tree.assets.empty());
    CHECK(tree.folders.size() == 2u);
    // Folders sorted: "models" before "textures".
    CHECK(tree.folders[0].name == "models");
    CHECK(tree.folders[1].name == "textures");

    const rb::AssetTree* textures = folder(tree, "textures");
    CHECK(textures != nullptr);
    if (textures != nullptr) {
        CHECK(textures->assets.size() == 2u);
        CHECK(textures->assets[0].name == "grass"); // sorted
        CHECK(textures->assets[1].name == "rock");
        CHECK(textures->path == std::filesystem::path("textures"));
    }
    const rb::AssetTree* models = folder(tree, "models");
    CHECK(models != nullptr);
    if (models != nullptr) {
        CHECK(models->assets.size() == 1u);
        CHECK(leaf(*models, "crate") != nullptr);
    }
}

static void deeplyNestedPathBuildsChain() {
    std::vector<rb::AssetDatabase::Record> records;
    records.push_back(rec(1, "/proj/assets/terrain/layers/snow.png", rb::AssetType::Texture, "snow"));
    const rb::AssetTree tree = rb::buildAssetTree("/proj/assets", records);

    const rb::AssetTree* terrain = folder(tree, "terrain");
    CHECK(terrain != nullptr);
    const rb::AssetTree* layers = terrain != nullptr ? folder(*terrain, "layers") : nullptr;
    CHECK(layers != nullptr);
    if (layers != nullptr) {
        CHECK(layers->path == std::filesystem::path("terrain") / "layers");
        const rb::AssetTree::Leaf* snow = leaf(*layers, "snow");
        CHECK(snow != nullptr);
        if (snow != nullptr) {
            const rb::Uuid expectedId{0u, 1u};
            CHECK(snow->type == rb::AssetType::Texture);
            CHECK(snow->id == expectedId);
            CHECK(snow->path == std::filesystem::path("/proj/assets/terrain/layers/snow.png"));
        }
    }
}

static void foldersSortCaseInsensitively() {
    std::vector<rb::AssetDatabase::Record> records;
    records.push_back(rec(1, "/proj/assets/Zoo/a.png", rb::AssetType::Texture, "a"));
    records.push_back(rec(2, "/proj/assets/alpha/b.png", rb::AssetType::Texture, "b"));
    const rb::AssetTree tree = rb::buildAssetTree("/proj/assets", records);
    CHECK(tree.folders.size() == 2u);
    // Case-insensitive: "alpha" sorts before "Zoo".
    CHECK(tree.folders[0].name == "alpha");
    CHECK(tree.folders[1].name == "Zoo");
}

static void outsideRootFiledAtTreeRoot() {
    std::vector<rb::AssetDatabase::Record> records;
    records.push_back(rec(1, "/elsewhere/orphan.png", rb::AssetType::Texture, "orphan"));
    records.push_back(rec(2, "/proj/assets/textures/home.png", rb::AssetType::Texture, "home"));
    const rb::AssetTree tree = rb::buildAssetTree("/proj/assets", records);
    // The orphan (no shared root) is filed at the tree root, not crashed on.
    CHECK(leaf(tree, "orphan") != nullptr);
    CHECK(folder(tree, "textures") != nullptr);
}

static void leafFallsBackToStemWhenUnnamed() {
    std::vector<rb::AssetDatabase::Record> records;
    records.push_back(rec(1, "/proj/assets/props/barrel.gltf", rb::AssetType::Model, ""));
    const rb::AssetTree tree = rb::buildAssetTree("/proj/assets", records);
    const rb::AssetTree* props = folder(tree, "props");
    CHECK(props != nullptr);
    if (props != nullptr) {
        CHECK(props->assets.size() == 1u);
        CHECK(props->assets[0].name == "barrel"); // stem fallback
    }
}

} // namespace

void assetTreeSuite() {
    emptyRecordsYieldEmptyTree();
    rootLevelAssetsLiveAtTreeRoot();
    nestedFoldersFromRelativePaths();
    deeplyNestedPathBuildsChain();
    foldersSortCaseInsensitively();
    outsideRootFiledAtTreeRoot();
    leafFallsBackToStemWhenUnnamed();
}

static void resolvesKnownAndFlagsMissing() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = rb::Uuid::generate();
    const rb::AssetHandle<rb::ModelAsset> asset = assets.add<rb::ModelAsset>(rb::ModelAsset{}, id);

    const rb::Entity known = runtime.scene().create();
    runtime.scene().add<rb::ModelRenderer>(known, rb::ModelRenderer{id, {}});

    const rb::Entity missing = runtime.scene().create();
    runtime.scene().add<rb::ModelRenderer>(missing, rb::ModelRenderer{rb::Uuid::generate(), {}});

    rb::AssetResolveSystem system;
    system.onUpdate(runtime, 0.016f);

    CHECK(runtime.scene().get<rb::ModelRenderer>(known).handle == asset);
    CHECK(runtime.scene().get<rb::ModelRenderer>(known).handle.valid());
    CHECK(!runtime.scene().get<rb::ModelRenderer>(missing).handle.valid());
}

static void reresolvesAfterReload() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = rb::Uuid::generate();
    const rb::AssetHandle<rb::ModelAsset> first = assets.add<rb::ModelAsset>(rb::ModelAsset{}, id);

    const rb::Entity e = runtime.scene().create();
    runtime.scene().add<rb::ModelRenderer>(e, rb::ModelRenderer{id, {}});

    rb::AssetResolveSystem system;
    system.onUpdate(runtime, 0.016f);
    CHECK(runtime.scene().get<rb::ModelRenderer>(e).handle == first);

    // Reload the asset: the old handle goes stale, a fresh one takes the same uuid.
    assets.erase<rb::ModelAsset>(first);
    const rb::AssetHandle<rb::ModelAsset> second = assets.add<rb::ModelAsset>(rb::ModelAsset{}, id);
    CHECK(!(second == first));

    system.onUpdate(runtime, 0.016f);
    CHECK(runtime.scene().get<rb::ModelRenderer>(e).handle == second);
}

// Models lazy-import from their registered source like every other resolve. Headless the
// import can only be exercised up to the file read (a real model upload needs GL), so a
// registered-but-missing source must leave the handle invalid without crashing or
// littering ghost sidecars, and keep getting retried safely.
static void modelResolveLazyLoadsFromSource() {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    const rb::Uuid id = rb::Uuid::generate();
    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "rabbet_resolve_missing.gltf";
    assets.registerSource(id, missing, rb::AssetType::Model);

    const rb::Entity e = runtime.scene().create();
    runtime.scene().add<rb::ModelRenderer>(e, rb::ModelRenderer{id, {}});

    rb::AssetResolveSystem system;
    system.onUpdate(runtime, 0.016f);
    system.onUpdate(runtime, 0.016f);

    CHECK(!runtime.scene().get<rb::ModelRenderer>(e).handle.valid());
    CHECK(!std::filesystem::exists(std::filesystem::path(missing.string() + ".import")));
}

// An empty ModelAsset needs no GL context, so the full reference -> serialize ->
// resolve loop can be exercised headlessly.
static void modelRendererRefSurvivesAndResolves() {
    rb::AssetManager assets;
    const rb::Uuid modelId = rb::Uuid::generate();
    const rb::AssetHandle<rb::ModelAsset> asset =
        assets.add<rb::ModelAsset>(rb::ModelAsset{}, modelId);
    CHECK(asset.valid());

    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    CHECK(registry.find("ModelRenderer") != nullptr);

    rb::Scene source;
    const rb::Entity e = source.create();
    source.add<rb::ModelRenderer>(e, rb::ModelRenderer{modelId, {}});

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);

    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);
    CHECK(loaded.count<rb::ModelRenderer>() == 1u);

    bool visited = false;
    loaded.each<rb::ModelRenderer>([&](rb::Entity, rb::ModelRenderer& renderer) {
        visited = true;
        CHECK(renderer.model == modelId);
        CHECK(!renderer.handle.valid());
        renderer.handle = assets.find<rb::ModelAsset>(renderer.model);
        CHECK(renderer.handle == asset);
        CHECK(assets.get<rb::ModelAsset>(renderer.handle) != nullptr);
    });
    CHECK(visited);
}

// A whole scene authored as a JSON file (no C++ entity building) loads into live
// entities, and its model reference resolves through the asset manager.
static void loadsSceneAuthoredAsData() {
    const rb::Uuid modelId = rb::Uuid::generate();

    const std::string sceneText = std::string(R"({
  "version": 1,
  "entities": [
    { "id": 0, "components": {
        "Name": { "value": "camera" },
        "Transform": { "position": [0.0, 1.0, 4.0], "rotation": [1.0, 0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
        "Camera": { "fovY": 1.0, "nearPlane": 0.1, "farPlane": 100.0 } } },
    { "id": 1, "components": {
        "Name": { "value": "sun" },
        "DirectionalLight": { "direction": [0.0, -1.0, -0.2], "color": [1.0, 1.0, 1.0], "intensity": 3.0 } } },
    { "id": 2, "components": {
        "Name": { "value": "prop" },
        "Transform": { "position": [0.0, 0.0, 0.0], "rotation": [1.0, 0.0, 0.0, 0.0], "scale": [1.0, 1.0, 1.0] },
        "ModelRenderer": { "model": ")") +
                                  modelId.toString() + R"(" } } }
  ]
})";

    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_showcase.scene.json";
    {
        std::ofstream out(path);
        out << sceneText;
    }

    rb::AssetManager assets;
    assets.add<rb::ModelAsset>(rb::ModelAsset{}, modelId); // stands in for a loaded model file

    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Scene scene;
    CHECK(rb::SceneSerializer::loadFromFile(scene, registry, path));
    CHECK(scene.aliveCount() == 3u);
    CHECK(scene.count<rb::Camera>() == 1u);
    CHECK(scene.count<rb::DirectionalLight>() == 1u);
    CHECK(scene.count<rb::ModelRenderer>() == 1u);
    CHECK(scene.count<rb::Name>() == 3u);

    bool resolved = false;
    scene.each<rb::ModelRenderer>([&](rb::Entity, rb::ModelRenderer& renderer) {
        CHECK(renderer.model == modelId);
        renderer.handle = assets.find<rb::ModelAsset>(renderer.model);
        resolved = renderer.handle.valid();
    });
    CHECK(resolved);

    std::error_code ec;
    std::filesystem::remove(path, ec);
}

void assetsResolveSuite() {
    resolvesKnownAndFlagsMissing();
    reresolvesAfterReload();
    modelResolveLazyLoadsFromSource();
    modelRendererRefSurvivesAndResolves();
    loadsSceneAuthoredAsData();
}

namespace {

constexpr rb::Uuid kA{0u, 1u};
constexpr rb::Uuid kB{0u, 2u};

static void freshCacheNeedsRender() {
    rb::ThumbnailCache cache;
    CHECK(cache.needsRender(kA, 0u));
    CHECK(!cache.contains(kA));
    CHECK(cache.size() == 0u);
}

static void markRenderedSatisfiesSameRevision() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 3u);
    CHECK(!cache.needsRender(kA, 3u));
    CHECK(cache.contains(kA));
    CHECK(cache.size() == 1u);
}

static void revisionBumpForcesRerender() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 1u);
    CHECK(cache.needsRender(kA, 2u)); // hot-reload bumped the revision
    cache.markRendered(kA, 2u);
    CHECK(!cache.needsRender(kA, 2u));
    CHECK(cache.size() == 1u); // re-render replaces, does not grow
}

static void invalidateForcesRerender() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 5u);
    cache.invalidate(kA);
    CHECK(cache.needsRender(kA, 5u));
    CHECK(!cache.contains(kA));
    CHECK(cache.size() == 0u);
}

static void clearEmptiesEverything() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 1u);
    cache.markRendered(kB, 1u);
    CHECK(cache.size() == 2u);
    cache.clear();
    CHECK(cache.size() == 0u);
    CHECK(cache.needsRender(kA, 1u));
    CHECK(cache.needsRender(kB, 1u));
}

static void uuidsAreIndependent() {
    rb::ThumbnailCache cache;
    cache.markRendered(kA, 2u);
    CHECK(!cache.needsRender(kA, 2u));
    CHECK(cache.needsRender(kB, 2u)); // B untouched
}

} // namespace

void thumbnailCacheSuite() {
    freshCacheNeedsRender();
    markRenderedSatisfiesSameRevision();
    revisionBumpForcesRerender();
    invalidateForcesRerender();
    clearEmptiesEverything();
    uuidsAreIndependent();
}

namespace {

namespace fs = std::filesystem;

rb::ComponentRegistry makeRegistry() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);
    return registry;
}

fs::path freshRoot(const char* leaf) {
    const fs::path root = fs::temp_directory_path() / leaf;
    std::error_code ec;
    // Heal a 0500 leftover from a killed run before removing, or remove_all aborts the binary.
    fs::permissions(root, fs::perms::owner_all, fs::perm_options::add, ec);
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    return root;
}

const rb::AssetDatabase::Record* findByName(const rb::AssetDatabase& db, const std::string& name) {
    for (const rb::AssetDatabase::Record& r : db.records()) {
        if (r.name == name) {
            return &r;
        }
    }
    return nullptr;
}

// Garbage characters flatten to underscores, empty input takes the fallback, and an absurd
// length is capped so "<base>_999<suffix>" can never brush a filesystem's 255-byte name limit.
void sanitizeCapsAndFallsBack() {
    CHECK(rb::sanitizeAssetName("wisp trail", "Script") == "wisp trail");
    CHECK(rb::sanitizeAssetName("a/b\\c:d", "Script") == "a_b_c_d");
    CHECK(rb::sanitizeAssetName("  .hidden.  ", "Script") == "hidden");
    CHECK(rb::sanitizeAssetName("", "Material") == "Material");
    CHECK(rb::sanitizeAssetName("...", "Scene") == "Scene");

    const fs::path scratch = freshRoot("rabbet_asset_create_sanitize_test");
    const std::string longName(300, 'x');
    const std::string capped = rb::sanitizeAssetName(longName, "Script");
    CHECK(capped.size() == 64u);

    // A cap cut landing on a dot re-trims instead of leaving a Windows-hostile trailing dot.
    std::string dotAtCut(63, 'y');
    dotAtCut += '.';
    dotAtCut += std::string(40, 'z');
    const std::string retrimmed = rb::sanitizeAssetName(dotAtCut, "Script");
    CHECK(!retrimmed.empty());
    CHECK(retrimmed.back() != '.');

    // The prefab path helper shares the discipline, which retires its NAME_MAX exposure.
    const fs::path prefab = rb::prefabFilePath(scratch, longName);
    CHECK(prefab.filename().string().size() <= 64u + std::string(".prefab.json").size());
    std::error_code ec;
    fs::remove_all(scratch, ec);
}

// Each template lands catalogued with the right type and a sidecar-assigned uuid.
void templatesCatalogueWithTheRightTypes() {
    const fs::path root = freshRoot("rabbet_asset_create_test");
    const rb::ComponentRegistry registry = makeRegistry();

    const fs::path script = rb::createScriptAsset(root / "scripts", "Mover");
    const fs::path material = rb::createMaterialAsset(root, "Shiny");
    const fs::path scene = rb::createSceneAsset(root, "Backdrop", registry);
    CHECK(!script.empty());
    CHECK(!material.empty());
    CHECK(!scene.empty());
    CHECK(script.filename() == "Mover.lua");
    CHECK(material.filename() == "Shiny.material.json");
    CHECK(scene.filename() == "Backdrop.scene.json");

    rb::AssetManager assets;
    rb::AssetDatabase db;
    CHECK(db.scan(root, &assets) == 3u);
    const rb::AssetDatabase::Record* mover = db.findByPath(script);
    const rb::AssetDatabase::Record* shiny = db.findByPath(material);
    const rb::AssetDatabase::Record* backdrop = db.findByPath(scene);
    CHECK(mover != nullptr && mover->type == rb::AssetType::Script && mover->id.valid());
    CHECK(shiny != nullptr && shiny->type == rb::AssetType::Material && shiny->id.valid());
    CHECK(backdrop != nullptr && backdrop->type == rb::AssetType::Scene && backdrop->id.valid());
    std::error_code ec;
    fs::remove_all(root, ec);
}

// The created scene passes the serializer's own shapeless-document gate, and the created
// material parses through the real import path.
void templatesLoadThroughTheRealGates() {
    const fs::path root = freshRoot("rabbet_asset_create_load_test");
    const rb::ComponentRegistry registry = makeRegistry();

    const fs::path scenePath = rb::createSceneAsset(root, "Empty", registry);
    CHECK(!scenePath.empty());
    rb::Scene scene;
    CHECK(rb::SceneSerializer::loadFromFile(scene, registry, scenePath));

    const fs::path materialPath = rb::createMaterialAsset(root, "Default");
    CHECK(!materialPath.empty());
    rb::AssetManager assets;
    CHECK(rb::loadMaterialAsset(assets, materialPath).valid());
    std::error_code ec;
    fs::remove_all(root, ec);
}

// The created script compiles and runs through the real script system.
void createdScriptCompiles() {
    const fs::path root = freshRoot("rabbet_asset_create_script_test");
    const fs::path script = rb::createScriptAsset(root, "Fresh");
    CHECK(!script.empty());

    rb::ComponentRegistry registry = makeRegistry();
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AssetDatabase& db = runtime.addResource<rb::AssetDatabase>();
    CHECK(db.scan(root, &assets) == 1u);
    const rb::AssetDatabase::Record* record = findByName(db, "Fresh");
    CHECK(record != nullptr);
    if (record == nullptr) {
        return;
    }

    runtime.addSystem<rb::ScriptAssetResolveSystem>();
    rb::ScriptSystem& scripts = runtime.addSystem<rb::ScriptSystem, rb::SystemPhase::Play>(
        &registry);
    rb::Scene& scene = runtime.scene();
    const rb::Entity e = scene.create();
    rb::ScriptComponent component;
    component.script = record->id;
    // instanceCount cannot distinguish a compiled script from a failed one (both occupy an
    // instance slot), so plant a field the template never declares: only a successful
    // compile merges the template's empty fields table over the component and clears it.
    rb::ScriptField sentinel;
    sentinel.name = "sentinel";
    sentinel.type = rb::ScriptField::Type::Number;
    sentinel.number = 1.0;
    component.fields.push_back(sentinel);
    scene.add<rb::ScriptComponent>(e, component);

    runtime.start();
    runtime.beginPlay(); // ScriptSystem is a Play-phase system; it only ticks in a session
    runtime.setPlaying(true);
    runtime.tick(0.1f);
    runtime.tick(0.1f);
    CHECK(scripts.instanceCount() == 1u);
    CHECK(scene.get<rb::ScriptComponent>(e).fields.empty()); // merged only when compile succeeds
    runtime.setPlaying(false);
    runtime.endPlay();
    runtime.stop();
    std::error_code cleanup;
    fs::remove_all(root, cleanup);
}

// A second create with the same name lands beside the first instead of overwriting it.
void collisionsDecollide() {
    const fs::path root = freshRoot("rabbet_asset_create_collide_test");
    const fs::path first = rb::createScriptAsset(root, "Same");
    const fs::path second = rb::createScriptAsset(root, "Same");
    CHECK(!first.empty());
    CHECK(!second.empty());
    CHECK(first != second);
    CHECK(second.filename() == "Same_1.lua");

    rb::AssetManager assets;
    rb::AssetDatabase db;
    CHECK(db.scan(root, &assets) == 2u);
    std::error_code ec;
    fs::remove_all(root, ec);
}

// An exhausted namespace refuses instead of renaming over the last candidate: with Full.lua and
// Full_1..Full_999.lua all present, the next create must not clobber Full_999 (the unguarded
// loop renamed the new file over it, destroying whatever the user had there).
void exhaustedCollisionsRefuse() {
    const fs::path root = freshRoot("rabbet_asset_create_full_test");
    const std::string keep = "-- user work, must survive\n";
    {
        std::ofstream out(root / "Full.lua");
        out << keep;
    }
    for (int i = 1; i < 1000; ++i) {
        std::ofstream out(root / ("Full_" + std::to_string(i) + ".lua"));
        out << keep;
    }

    const fs::path created = rb::createScriptAsset(root, "Full");
    CHECK(created.empty());

    std::ifstream in(root / "Full_999.lua");
    std::string firstLine;
    std::getline(in, firstLine);
    CHECK(firstLine == "-- user work, must survive");
    std::error_code ec;
    fs::remove_all(root, ec);
}

// An unwritable directory reports failure with an empty path instead of half-creating.
// Root ignores permission bits and fails this loudly, which beats asserting nothing.
void unwritableDirectoryRefuses() {
    const fs::path root = freshRoot("rabbet_asset_create_denied_test");
    std::error_code ec;
    fs::permissions(root, fs::perms::owner_read | fs::perms::owner_exec,
                    fs::perm_options::replace, ec);
    const fs::path created = rb::createScriptAsset(root, "Nope");
    fs::permissions(root, fs::perms::owner_all, fs::perm_options::replace, ec);
    CHECK(created.empty());
    CHECK(fs::is_empty(root, ec));
    fs::remove_all(root, ec);
}

} // namespace

void assetCreateSuite() {
    sanitizeCapsAndFallsBack();
    templatesCatalogueWithTheRightTypes();
    templatesLoadThroughTheRealGates();
    createdScriptCompiles();
    collisionsDecollide();
    exhaustedCollisionsRefuse();
    unwritableDirectoryRefuses();
}

int main() {
    assetsMetaSuite();
    assetsManagerSuite();
    assetsDatabaseSuite();
    assetTreeSuite();
    assetsResolveSuite();
    thumbnailCacheSuite();
    assetCreateSuite();
    return rbtest::summary("assets");
}

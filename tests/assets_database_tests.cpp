#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetManager.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Uuid.h"
#include "tests/Test.h"

#include <filesystem>
#include <fstream>
#include <system_error>

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

int main() {
    extensionClassification();
    scanCataloguesAssets();
    uuidsAreStableAcrossScans();
    missingDirectoryIsEmpty();
    return rbtest::summary("assets_database");
}

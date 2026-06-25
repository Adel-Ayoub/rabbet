#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetTree.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Uuid.h"
#include "tests/Test.h"

#include <filesystem>
#include <string>
#include <vector>

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

int main() {
    emptyRecordsYieldEmptyTree();
    rootLevelAssetsLiveAtTreeRoot();
    nestedFoldersFromRelativePaths();
    deeplyNestedPathBuildsChain();
    foldersSortCaseInsensitively();
    outsideRootFiledAtTreeRoot();
    leafFallsBackToStemWhenUnnamed();
    return rbtest::summary("asset_tree");
}

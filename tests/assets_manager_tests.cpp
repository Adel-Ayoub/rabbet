#include "rabbet/assets/AssetManager.h"
#include "tests/Test.h"

#include <filesystem>
#include <fstream>
#include <optional>
#include <system_error>

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

int main() {
    addGetErase();
    reusedSlotInvalidatesOldHandle();
    lookupByUuidIsTypeSafe();
    loadCachesByPath();
    loadDedupsEvenWithCorruptSidecar();
    return rbtest::summary("assets_manager");
}

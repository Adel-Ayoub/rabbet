#include "rabbet/assets/AssetMeta.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Uuid.h"
#include "tests/Test.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

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
    const std::string contents((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
    CHECK(contents.find("not valid json") != std::string::npos);

    std::filesystem::remove(sidecar, ec);
    std::filesystem::remove(asset, ec);
}

int main() {
    metadataPersistsAcrossRuns();
    uuidHelperStillWorks();
    absentSourceNeedsNoReimport();
    corruptSidecarIsNotOverwritten();
    return rbtest::summary("assets_meta");
}

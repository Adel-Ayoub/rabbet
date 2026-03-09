#include "rabbet/assets/AssetMeta.h"
#include "rabbet/core/Uuid.h"
#include "tests/Test.h"

#include <filesystem>
#include <system_error>

static void uuidPersistsAcrossRuns() {
    const std::filesystem::path asset =
        std::filesystem::temp_directory_path() / "rabbet_meta_asset.gltf";
    const std::filesystem::path sidecar = rb::assetmeta::sidecarPath(asset);

    std::error_code ec;
    std::filesystem::remove(sidecar, ec);

    const rb::Uuid first = rb::assetmeta::loadOrCreateUuid(asset);
    CHECK(first.valid());
    CHECK(std::filesystem::exists(sidecar));

    // A second call stands in for a later run: it must read the sidecar, not reassign.
    const rb::Uuid second = rb::assetmeta::loadOrCreateUuid(asset);
    CHECK(first == second);

    std::filesystem::remove(sidecar, ec);
}

int main() {
    uuidPersistsAcrossRuns();
    return rbtest::summary("assets_meta");
}

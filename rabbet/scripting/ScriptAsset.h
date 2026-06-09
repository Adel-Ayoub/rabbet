#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "rabbet/assets/AssetType.h"

namespace rb {

// The text of a .lua file, owned by the AssetManager and referenced by uuid. `revision`
// bumps each time the source is hot-reloaded from disk so the ScriptSystem can recompile
// live instances; `sourceTimestamp` is the file mtime the current text was read at.
struct ScriptAsset {
    std::string source;
    std::filesystem::path path;
    std::int64_t sourceTimestamp = 0;
    std::uint32_t revision = 0;
};

template <>
[[nodiscard]] constexpr AssetType assetTypeFor<ScriptAsset>() noexcept {
    return AssetType::Script;
}

} // namespace rb

#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Uuid.h"

namespace rb::assetmeta {

struct Metadata {
    Uuid id;
    AssetType type = AssetType::Unknown;
    std::string name;
    std::vector<Uuid> dependencies;
};

[[nodiscard]] std::filesystem::path sidecarPath(const std::filesystem::path& assetPath);

// Reads the asset's ".import" sidecar. If it is missing or unreadable, creates
// fresh metadata (new uuid, name from the file stem) and writes it, so the asset
// keeps a stable identity across runs.
[[nodiscard]] Metadata loadOrCreate(const std::filesystem::path& assetPath, AssetType type);

void write(const std::filesystem::path& assetPath, const Metadata& meta);

// True when the source file exists and is newer than its sidecar. The sidecar's own
// mtime is the import time, so nothing wall-clock has to be stored inside it.
[[nodiscard]] bool needsReimport(const std::filesystem::path& assetPath);

[[nodiscard]] Uuid loadOrCreateUuid(const std::filesystem::path& assetPath);

} // namespace rb::assetmeta

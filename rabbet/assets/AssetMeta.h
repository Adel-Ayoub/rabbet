#pragma once

#include <filesystem>

#include "rabbet/core/Uuid.h"

namespace rb::assetmeta {

[[nodiscard]] std::filesystem::path sidecarPath(const std::filesystem::path& assetPath);

// Returns the uuid recorded in the asset's ".import" sidecar. If the sidecar is
// missing or unreadable, generates a new uuid and writes it, so the asset keeps a
// stable identity across runs.
[[nodiscard]] Uuid loadOrCreateUuid(const std::filesystem::path& assetPath);

} // namespace rb::assetmeta

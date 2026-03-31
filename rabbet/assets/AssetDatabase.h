#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Uuid.h"

namespace rb {

class AssetManager;

// A catalogue of a project's asset files. Scanning reads (or creates) each asset's
// ".import" sidecar to learn its stable uuid, name, and type, then records the
// uuid<->path mapping. It loads no asset bytes: the AssetManager imports on demand
// using the paths registered here. Intended to live as a Runtime resource.
class AssetDatabase {
public:
    struct Record {
        Uuid id;
        std::filesystem::path path;
        AssetType type = AssetType::Unknown;
        std::string name;
    };

    // Recursively catalogues every file under `root` whose extension maps to a known
    // AssetType, returning the count. A re-scan clears the previous catalogue first.
    // When `manager` is non-null, each asset's uuid<->path is registered with it so
    // scene references can be resolved and lazily imported by uuid.
    std::size_t scan(const std::filesystem::path& root, AssetManager* manager = nullptr);

    [[nodiscard]] const std::vector<Record>& records() const noexcept { return m_records; }
    [[nodiscard]] const Record* find(const Uuid& id) const noexcept;
    [[nodiscard]] const Record* findByPath(const std::filesystem::path& path) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return m_records.size(); }
    [[nodiscard]] const std::filesystem::path& root() const noexcept { return m_root; }

private:
    std::filesystem::path m_root;
    std::vector<Record> m_records;
    std::unordered_map<Uuid, std::size_t> m_byId;
    std::unordered_map<std::string, std::size_t> m_byPath;
};

// Classifies an asset file by its (case-insensitive) extension. Recognises the
// compound ".scene.json" / ".prefab.json" forms before falling back to the simple
// extension. Returns AssetType::Unknown for files that are not assets.
[[nodiscard]] AssetType assetTypeFromExtension(const std::filesystem::path& path);

} // namespace rb

#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/assets/AssetType.h"
#include "rabbet/core/Uuid.h"

namespace rb {

// A node in the virtual-path folder tree built from an AssetDatabase's records. Each folder lists
// its child folders followed by the asset leaves directly inside it; both are sorted by name so the
// browser order is stable across scans. A leaf carries just enough to drive the browser; the full
// Record is recovered via AssetDatabase::find(id) when needed.
struct AssetTree {
    struct Leaf {
        Uuid id;
        AssetType type = AssetType::Unknown;
        std::string name;           // display name (Record.name, or the file stem)
        std::filesystem::path path; // on-disk path (Record.path)
    };

    std::string name;           // folder name ("" for the root)
    std::filesystem::path path; // folder path relative to the database root ("" for the root)
    std::vector<AssetTree> folders;
    std::vector<Leaf> assets;

    [[nodiscard]] bool empty() const noexcept { return folders.empty() && assets.empty(); }
};

// Builds the folder tree from records whose paths live under `root`, computed purely from the
// lexical relative path (no filesystem access). A record that does not sit under `root` is filed at
// the tree root under its filename. Folders and leaves are each sorted by name (case-insensitive).
[[nodiscard]] AssetTree buildAssetTree(const std::filesystem::path& root,
                                       std::span<const AssetDatabase::Record> records);

} // namespace rb

#pragma once

#include <string_view>

namespace rb {

enum class AssetType { Unknown, Texture, Model, Material, Mesh, Scene, Prefab };

[[nodiscard]] constexpr std::string_view assetTypeName(AssetType type) noexcept {
    switch (type) {
    case AssetType::Texture:
        return "Texture";
    case AssetType::Model:
        return "Model";
    case AssetType::Material:
        return "Material";
    case AssetType::Mesh:
        return "Mesh";
    case AssetType::Scene:
        return "Scene";
    case AssetType::Prefab:
        return "Prefab";
    case AssetType::Unknown:
        break;
    }
    return "Unknown";
}

[[nodiscard]] constexpr AssetType assetTypeFromName(std::string_view name) noexcept {
    if (name == "Texture") {
        return AssetType::Texture;
    }
    if (name == "Model") {
        return AssetType::Model;
    }
    if (name == "Material") {
        return AssetType::Material;
    }
    if (name == "Mesh") {
        return AssetType::Mesh;
    }
    if (name == "Scene") {
        return AssetType::Scene;
    }
    if (name == "Prefab") {
        return AssetType::Prefab;
    }
    return AssetType::Unknown;
}

// Maps an asset C++ type to its runtime tag. Specialized next to each asset type.
template <typename T>
[[nodiscard]] constexpr AssetType assetTypeFor() noexcept {
    return AssetType::Unknown;
}

} // namespace rb

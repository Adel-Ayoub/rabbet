#pragma once

#include "rabbet/assets/AssetType.h"
#include "rabbet/render/gl/Texture.h"

namespace rb {

struct TextureAsset {
    gl::Texture texture;
};

template <>
[[nodiscard]] constexpr AssetType assetTypeFor<TextureAsset>() noexcept {
    return AssetType::Texture;
}

} // namespace rb

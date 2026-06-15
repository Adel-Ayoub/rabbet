#include "rabbet/render/TextureImport.h"

#include <optional>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/render/Image.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/render/TextureAsset.h"
#include "rabbet/render/gl/Texture.h"

namespace rb {

AssetHandle<TextureAsset> loadTextureAsset(AssetManager& assets,
                                           const std::filesystem::path& path) {
    return assets.load<TextureAsset>(
        path, [](const std::filesystem::path& p) -> std::optional<TextureAsset> {
            std::optional<Image> image = loadImage(p);
            if (!image.has_value()) {
                return std::nullopt;
            }
            gl::TextureConfig config;
            config.srgb = true;
            return TextureAsset{gl::Texture::fromPixels(image->pixels, image->width, image->height,
                                                        image->channels, config)};
        });
}

} // namespace rb

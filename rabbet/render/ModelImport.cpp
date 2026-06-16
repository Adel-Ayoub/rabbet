#include "rabbet/render/ModelImport.h"

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include <glm/glm.hpp>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/render/Image.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/render/Model.h"
#include "rabbet/render/ModelLoader.h"
#include "rabbet/render/TextureAsset.h"
#include "rabbet/render/gl/Texture.h"
#include "rabbet/util/Log.h"

namespace rb {
namespace {

AssetHandle<TextureAsset> uploadAlbedo(AssetManager& assets, const std::filesystem::path& path) {
    if (std::optional<Image> image = loadImage(path)) {
        gl::TextureConfig config;
        config.srgb = true;
        return assets.add<TextureAsset>(TextureAsset{gl::Texture::fromPixels(
            image->pixels, image->width, image->height, image->channels, config)});
    }
    return AssetHandle<TextureAsset>{};
}

} // namespace

std::optional<ModelAsset> importModel(AssetManager& assets, const std::filesystem::path& path,
                                      const ModelImportOptions& options) {
    const std::optional<Model> model = loadModel(path);
    if (!model) {
        return std::nullopt;
    }
    const std::filesystem::path dir = path.parent_path();

    // The white fallback texture is created lazily so a fully-textured model never
    // allocates (and leaks) an unused 1x1 texture in the asset store.
    std::optional<AssetHandle<TextureAsset>> white;
    const auto whiteHandle = [&]() -> AssetHandle<TextureAsset> {
        if (!white.has_value()) {
            white = assets.add<TextureAsset>(TextureAsset{gl::Texture::solid(255, 255, 255)});
        }
        return *white;
    };

    std::unordered_map<std::string, AssetHandle<TextureAsset>> textures;

    ModelAsset result;
    result.submeshes.reserve(model->meshes.size());
    for (const ModelMesh& part : model->meshes) {
        const ModelMaterial* material =
            (part.materialIndex >= 0 &&
             static_cast<std::size_t>(part.materialIndex) < model->materials.size())
                ? &model->materials[static_cast<std::size_t>(part.materialIndex)]
                : nullptr;

        ModelAsset::Submesh submesh{gl::Mesh::create(part.data), glm::vec3(1.0f), glm::vec3(0.0f),
                                    options.metallic, options.roughness, 1.0f,
                                    AssetHandle<TextureAsset>{}};
        if (material != nullptr) {
            submesh.emissive = material->emissive;
        }
        if (material != nullptr && !material->baseColorTexture.empty()) {
            auto it = textures.find(material->baseColorTexture);
            if (it == textures.end()) {
                const AssetHandle<TextureAsset> tex =
                    uploadAlbedo(assets, dir / material->baseColorTexture);
                it = textures.emplace(material->baseColorTexture, tex.valid() ? tex : whiteHandle())
                         .first;
            }
            submesh.albedo = it->second;
        } else {
            if (material != nullptr) {
                submesh.baseColor = material->baseColor;
            }
            submesh.albedo = whiteHandle();
        }
        result.submeshes.push_back(std::move(submesh));
    }

    log::info("imported model '{}': {} submesh(es)", path.string(), result.submeshes.size());
    return result;
}

AssetHandle<ModelAsset> loadModelAsset(AssetManager& assets, const std::filesystem::path& path,
                                       const ModelImportOptions& options) {
    return assets.load<ModelAsset>(path, [&assets, &options](const std::filesystem::path& p) {
        return importModel(assets, p, options);
    });
}

} // namespace rb

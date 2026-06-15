#include "rabbet/render/MaterialAssetResolveSystem.h"

#include <filesystem>
#include <optional>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/MaterialComponent.h"
#include "rabbet/render/MaterialImport.h"
#include "rabbet/render/ShaderAsset.h"
#include "rabbet/render/ShaderImport.h"
#include "rabbet/render/TextureAsset.h"
#include "rabbet/render/TextureImport.h"

namespace rb {
namespace {

void resolveDependencies(AssetManager& assets, MaterialAsset& material) {
    if (material.shader.valid()) {
        material.shaderHandle = assets.find<ShaderAsset>(material.shader);
        if (!material.shaderHandle.valid()) {
            if (const std::optional<std::filesystem::path> path = assets.sourcePath(material.shader)) {
                material.shaderHandle = loadShaderAsset(assets, *path);
            }
        }
        reloadShaderIfChanged(assets, material.shaderHandle);
    } else {
        material.shaderHandle = {};
    }

    for (MaterialTexture& texture : material.textures) {
        if (!texture.texture.valid()) {
            texture.handle = {};
            continue;
        }
        texture.handle = assets.find<TextureAsset>(texture.texture);
        if (!texture.handle.valid()) {
            if (const std::optional<std::filesystem::path> path = assets.sourcePath(texture.texture)) {
                texture.handle = loadTextureAsset(assets, *path);
            }
        }
    }
}

} // namespace

void MaterialAssetResolveSystem::onUpdate(Runtime& runtime, float) {
    AssetManager* assets = runtime.tryResource<AssetManager>();
    if (assets == nullptr) {
        return;
    }
    runtime.scene().each<MaterialComponent>([assets](Entity, MaterialComponent& component) {
        if (!component.material.valid()) {
            component.handle = {};
            return;
        }
        component.handle = assets->find<MaterialAsset>(component.material);
        if (!component.handle.valid()) {
            if (const std::optional<std::filesystem::path> path =
                    assets->sourcePath(component.material)) {
                component.handle = loadMaterialAsset(*assets, *path);
            }
        }
        // Re-fetch after any lazy load above so the pointer is into the current store backing.
        if (MaterialAsset* material = assets->get<MaterialAsset>(component.handle)) {
            reloadMaterialIfChanged(*assets, component.handle);
            resolveDependencies(*assets, *material);
        }
    });
}

} // namespace rb

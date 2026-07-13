#include "rabbet/render/AssetResolveSystem.h"

#include <filesystem>
#include <optional>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelImport.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/TextureAsset.h"
#include "rabbet/render/TextureImport.h"
#include "rabbet/terrain/TerrainComponent.h"

namespace rb {

void AssetResolveSystem::onUpdate(Runtime& runtime, float) {
    AssetManager* assets = runtime.tryResource<AssetManager>();
    if (assets == nullptr) {
        return;
    }
    // Models lazy-import like every other resolve: a scene loaded outside the editor's
    // startup preload path (a runtime scene switch) must still get its meshes, not the
    // magenta placeholder forever.
    runtime.scene().each<ModelRenderer>([assets](Entity, ModelRenderer& renderer) {
        if (!renderer.model.valid()) {
            renderer.handle = {};
            return;
        }
        renderer.handle = assets->find<ModelAsset>(renderer.model);
        if (!renderer.handle.valid()) {
            if (const std::optional<std::filesystem::path> path =
                    assets->sourcePath(renderer.model)) {
                renderer.handle = loadModelAsset(*assets, *path);
            }
        }
    });
    // Particle sprites are optional standalone textures: resolve the uuid, importing once from the
    // catalogued source path if it is not loaded yet (the same find-then-load cascade material
    // textures use).
    runtime.scene().each<ParticleEmitter>([assets](Entity, ParticleEmitter& emitter) {
        if (!emitter.sprite.valid()) {
            emitter.handle = {};
            return;
        }
        emitter.handle = assets->find<TextureAsset>(emitter.sprite);
        if (!emitter.handle.valid()) {
            if (const std::optional<std::filesystem::path> path =
                    assets->sourcePath(emitter.sprite)) {
                emitter.handle = loadTextureAsset(*assets, *path);
            }
        }
    });
    // Terrain layer albedos + splat map resolve like particle sprites (find then lazy-load from the
    // catalogued source). The heightmap is NOT resolved here: TerrainSystem reads its pixels CPU-side
    // for mesh generation, never as a bound GL texture.
    const auto resolveTexture = [assets](const Uuid& id) -> AssetHandle<TextureAsset> {
        if (!id.valid()) {
            return {};
        }
        AssetHandle<TextureAsset> handle = assets->find<TextureAsset>(id);
        if (!handle.valid()) {
            if (const std::optional<std::filesystem::path> path = assets->sourcePath(id)) {
                handle = loadTextureAsset(*assets, *path);
            }
        }
        return handle;
    };
    runtime.scene().each<TerrainComponent>([&resolveTexture](Entity, TerrainComponent& terrain) {
        for (int i = 0; i < terrain.layerCount && i < TerrainComponent::kMaxLayers; ++i) {
            TerrainLayer& layer = terrain.layers[static_cast<std::size_t>(i)];
            layer.handle = resolveTexture(layer.albedo);
        }
        terrain.splatHandle = resolveTexture(terrain.splat);
    });
}

} // namespace rb

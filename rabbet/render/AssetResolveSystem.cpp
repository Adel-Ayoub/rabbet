#include "rabbet/render/AssetResolveSystem.h"

#include <filesystem>
#include <optional>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/particle/ParticleEmitter.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelRenderer.h"
#include "rabbet/render/TextureAsset.h"
#include "rabbet/render/TextureImport.h"

namespace rb {

void AssetResolveSystem::onUpdate(Runtime& runtime, float) {
    AssetManager* assets = runtime.tryResource<AssetManager>();
    if (assets == nullptr) {
        return;
    }
    runtime.scene().each<ModelRenderer>([assets](Entity, ModelRenderer& renderer) {
        renderer.handle = assets->find<ModelAsset>(renderer.model);
    });
    // Particle sprites are optional standalone textures: resolve the uuid, importing once from the
    // catalogued source path if it is not loaded yet (the same find-then-load cascade material
    // textures use). Models upload their own albedo at import, so they only need the find above.
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
}

} // namespace rb

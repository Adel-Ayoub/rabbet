#include "rabbet/render/AssetResolveSystem.h"

#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/render/ModelAsset.h"
#include "rabbet/render/ModelRenderer.h"

namespace rb {

void AssetResolveSystem::onUpdate(Runtime& runtime, float) {
    AssetManager* assets = runtime.tryResource<AssetManager>();
    if (assets == nullptr) {
        return;
    }
    runtime.scene().each<ModelRenderer>([assets](Entity, ModelRenderer& renderer) {
        renderer.handle = assets->find<ModelAsset>(renderer.model);
    });
}

} // namespace rb

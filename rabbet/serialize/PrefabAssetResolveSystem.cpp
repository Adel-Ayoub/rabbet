#include "rabbet/serialize/PrefabAssetResolveSystem.h"

#include <filesystem>
#include <optional>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/serialize/Prefab.h"
#include "rabbet/serialize/PrefabAsset.h"
#include "rabbet/serialize/PrefabInstance.h"

namespace rb {

void PrefabAssetResolveSystem::onUpdate(Runtime& runtime, float) {
    AssetManager* assets = runtime.tryResource<AssetManager>();
    if (assets == nullptr) {
        return;
    }
    runtime.scene().each<PrefabInstance>([assets](Entity, PrefabInstance& instance) {
        if (!instance.prefab.valid()) {
            instance.handle = {};
            return;
        }
        instance.handle = assets->find<PrefabAsset>(instance.prefab);
        if (!instance.handle.valid()) {
            if (const std::optional<std::filesystem::path> path = assets->sourcePath(instance.prefab)) {
                instance.handle = loadPrefabAsset(*assets, *path);
            }
        }
    });
}

} // namespace rb

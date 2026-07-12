#include "rabbet/scripting/ScriptAssetResolveSystem.h"

#include <filesystem>
#include <optional>
#include <unordered_set>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/scripting/ScriptAsset.h"
#include "rabbet/scripting/ScriptComponent.h"
#include "rabbet/scripting/ScriptImport.h"

namespace rb {

void ScriptAssetResolveSystem::onUpdate(Runtime& runtime, float) {
    AssetManager* assets = runtime.tryResource<AssetManager>();
    if (assets == nullptr) {
        return;
    }
    // The mtime poll is a filesystem stat; a hundred spawned entities sharing one .lua
    // must not stat the same file a hundred times a frame, so poll each script once.
    std::unordered_set<Uuid> polled;
    runtime.scene().each<ScriptComponent>([assets, &polled](Entity, ScriptComponent& script) {
        if (!script.script.valid()) {
            script.handle = {};
            return;
        }
        script.handle = assets->find<ScriptAsset>(script.script);
        // Lazy import from the catalogued source, like the audio/material/prefab resolves,
        // so a scene loaded from disk resolves its scripts without an editor assign step.
        if (!script.handle.valid()) {
            if (const std::optional<std::filesystem::path> path =
                    assets->sourcePath(script.script)) {
                script.handle = loadScriptAsset(*assets, *path);
            }
        }
        if (polled.insert(script.script).second) {
            reloadScriptIfChanged(*assets, script.handle);
        }
    });
}

} // namespace rb

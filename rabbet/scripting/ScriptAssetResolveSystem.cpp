#include "rabbet/scripting/ScriptAssetResolveSystem.h"

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
    runtime.scene().each<ScriptComponent>([assets](Entity, ScriptComponent& script) {
        script.handle = assets->find<ScriptAsset>(script.script);
        reloadScriptIfChanged(*assets, script.handle);
    });
}

} // namespace rb

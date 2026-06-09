#pragma once

#include <vector>

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/scripting/ScriptField.h"

namespace rb {

struct ScriptAsset;

// Attaches Lua behavior to an entity. `script` is the stable .lua asset id stored in
// scene files; `handle` is its runtime resolution, populated by ScriptAssetResolveSystem.
// `fields` are the script's exposed, inspector-editable values, persisted alongside it.
struct ScriptComponent {
    Uuid script;
    AssetHandle<ScriptAsset> handle;
    std::vector<ScriptField> fields;
};

} // namespace rb

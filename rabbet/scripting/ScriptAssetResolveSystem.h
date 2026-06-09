#pragma once

#include "rabbet/core/System.h"

namespace rb {

// Resolves ScriptComponent asset references (uuid -> handle) against the AssetManager
// each frame, and polls each referenced .lua file for on-disk edits so they hot-reload.
// Runs in the Always phase so a reload is picked up even while not playing; the
// ScriptSystem recompiles affected instances on the next Play tick.
class ScriptAssetResolveSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;
};

} // namespace rb

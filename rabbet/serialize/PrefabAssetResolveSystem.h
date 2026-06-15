#pragma once

#include "rabbet/core/System.h"

namespace rb {

// Resolves PrefabInstance asset references (uuid -> handle) against the AssetManager each frame,
// lazily importing from the registered source path so a freshly-loaded scene's instances link
// back to their prefab (for revert / display) without a manual re-assign. Runs in the Always
// phase. The components themselves already live in the scene; this only restores the link.
class PrefabAssetResolveSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;
};

} // namespace rb

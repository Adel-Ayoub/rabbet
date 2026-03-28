#pragma once

#include "rabbet/core/System.h"

namespace rb {

// Resolves ModelRenderer asset references (uuid -> handle) against the AssetManager
// resource each frame. Re-resolving every frame keeps handles correct across asset
// reloads; an unresolved reference leaves the handle invalid so the renderer can flag it.
class AssetResolveSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;
};

} // namespace rb

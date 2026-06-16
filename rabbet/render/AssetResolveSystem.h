#pragma once

#include "rabbet/core/System.h"

namespace rb {

// Resolves render asset references (uuid -> handle) against the AssetManager resource each frame:
// ModelRenderer models and ParticleEmitter sprites. Re-resolving every frame keeps handles correct
// across asset reloads; an unresolved reference leaves the handle invalid so the renderer can fall
// back (a magenta placeholder mesh, or the built-in soft dot for particles).
class AssetResolveSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;
};

} // namespace rb

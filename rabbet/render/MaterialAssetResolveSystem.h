#pragma once

#include "rabbet/core/System.h"

namespace rb {

// Resolves MaterialComponent asset references (uuid -> handle) against the AssetManager each
// frame, lazily importing from the registered source path so a freshly-loaded scene's materials
// resolve without a manual re-assign. It also cascades into each resolved material — linking its
// shader and texture handles (lazy-importing those too) and polling the shader file's mtime for
// hot reload (the .lua pattern). Runs in the Always phase so handles are current before the
// RenderSystem draws.
class MaterialAssetResolveSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;
};

} // namespace rb

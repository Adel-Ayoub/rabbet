#pragma once

#include "rabbet/core/System.h"

namespace rb {

// Resolves SoundEmitter asset references (uuid -> handle) against the AssetManager each frame,
// lazily importing from the registered source path so a freshly-loaded scene's emitters
// resolve without a manual re-assign. Runs in the Always phase so the handle is current
// before the AudioSystem builds voices on the next Play.
class AudioAssetResolveSystem final : public System {
public:
    void onUpdate(Runtime& runtime, float dt) override;
};

} // namespace rb

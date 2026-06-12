#pragma once

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"

namespace rb {

struct AudioAsset;

// Plays an audio clip from an entity during Play mode. `sound` is the stable asset id stored
// in scene files; `handle` is its runtime resolution, populated by AudioAssetResolveSystem.
// When `spatial`, the voice is positioned at the entity's Transform and attenuates/pans
// relative to the active camera listener; otherwise it plays as a flat 2D sound. The clip
// starts on the play-session begin edge when `playOnStart`. Runtime voice state lives in the
// AudioSystem (keyed by entity), so this stays plain data.
struct SoundEmitter {
    Uuid sound;
    AssetHandle<AudioAsset> handle;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool spatial = false;
    bool playOnStart = true;
};

} // namespace rb

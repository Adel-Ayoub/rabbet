#pragma once

#include "rabbet/assets/AssetHandle.h"
#include "rabbet/core/Uuid.h"

namespace rb {

struct AudioAsset;

// Plays an audio clip from an entity during Play mode. `sound` is the stable asset id stored
// in scene files; `handle` is its runtime resolution, populated by AudioAssetResolveSystem.
// When `spatial`, the voice is positioned at the entity's Transform and attenuates/pans
// relative to the active camera listener; otherwise it plays as a flat 2D sound. The clip
// starts on the play-session begin edge when `playOnStart`. `stream` reads the clip from disk
// on demand instead of decoding it up front — set it for long music tracks so pressing Play
// doesn't stall on a full decode; leave it off for short SFX. Runtime voice state lives in the
// AudioSystem (keyed by entity), so this stays plain data.
struct SoundEmitter {
    Uuid sound;
    AssetHandle<AudioAsset> handle;
    float volume = 1.0f;
    float pitch = 1.0f;
    bool loop = false;
    bool spatial = false;
    bool playOnStart = true;
    bool stream = false;
};

} // namespace rb

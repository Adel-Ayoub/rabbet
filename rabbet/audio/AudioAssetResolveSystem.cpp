#include "rabbet/audio/AudioAssetResolveSystem.h"

#include <filesystem>
#include <optional>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/audio/AudioAsset.h"
#include "rabbet/audio/AudioImport.h"
#include "rabbet/audio/SoundEmitter.h"
#include "rabbet/core/Runtime.h"

namespace rb {

void AudioAssetResolveSystem::onUpdate(Runtime& runtime, float) {
    AssetManager* assets = runtime.tryResource<AssetManager>();
    if (assets == nullptr) {
        return;
    }
    runtime.scene().each<SoundEmitter>([assets](Entity, SoundEmitter& emitter) {
        if (!emitter.sound.valid()) {
            emitter.handle = {};
            return;
        }
        emitter.handle = assets->find<AudioAsset>(emitter.sound);
        if (!emitter.handle.valid()) {
            if (const std::optional<std::filesystem::path> path = assets->sourcePath(emitter.sound)) {
                emitter.handle = loadAudioAsset(*assets, *path);
            }
        }
    });
}

} // namespace rb

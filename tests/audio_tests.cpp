#include "rabbet/assets/AssetManager.h"
#include "rabbet/audio/AudioAsset.h"
#include "rabbet/audio/AudioAssetResolveSystem.h"
#include "rabbet/audio/AudioImport.h"
#include "rabbet/audio/AudioSystem.h"
#include "rabbet/audio/SoundEmitter.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <nlohmann/json.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;

// Write a minimal valid PCM16 mono WAV (silent) so miniaudio's wav decoder can load a real
// clip without bundling an asset or touching an audio device.
void writeWav(const fs::path& path) {
    const std::uint32_t sampleRate = 8000u;
    const std::uint16_t channels = 1u;
    const std::uint16_t bitsPerSample = 16u;
    const std::uint32_t blockAlign = static_cast<std::uint32_t>(channels) * (bitsPerSample / 8u);
    const std::uint32_t byteRate = sampleRate * blockAlign;
    const std::uint32_t numSamples = 4000u; // 0.5s
    const std::uint32_t dataSize = numSamples * blockAlign;

    std::ofstream out(path, std::ios::binary);
    const auto u32 = [&](std::uint32_t v) {
        const unsigned char b[4] = {static_cast<unsigned char>(v & 0xFFu),
                                    static_cast<unsigned char>((v >> 8) & 0xFFu),
                                    static_cast<unsigned char>((v >> 16) & 0xFFu),
                                    static_cast<unsigned char>((v >> 24) & 0xFFu)};
        out.write(reinterpret_cast<const char*>(b), 4);
    };
    const auto u16 = [&](std::uint16_t v) {
        const unsigned char b[2] = {static_cast<unsigned char>(v & 0xFFu),
                                    static_cast<unsigned char>((v >> 8) & 0xFFu)};
        out.write(reinterpret_cast<const char*>(b), 2);
    };

    out.write("RIFF", 4);
    u32(36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    u32(16u); // PCM fmt chunk size
    u16(1u);  // PCM
    u16(channels);
    u32(sampleRate);
    u32(byteRate);
    u16(static_cast<std::uint16_t>(blockAlign));
    u16(bitsPerSample);
    out.write("data", 4);
    u32(dataSize);
    const std::vector<char> samples(dataSize, 0);
    out.write(samples.data(), static_cast<std::streamsize>(dataSize));
}

rb::Entity addEmitter(rb::Runtime& runtime, rb::AssetManager& assets, const fs::path& wav,
                      const glm::vec3& position, bool spatial, bool playOnStart, bool loop = false,
                      bool stream = false) {
    const rb::AssetHandle<rb::AudioAsset> handle = rb::loadAudioAsset(assets, wav);
    const rb::Entity e = runtime.scene().create();
    rb::Transform transform;
    transform.position = position;
    runtime.scene().add<rb::Transform>(e, transform);
    rb::SoundEmitter emitter;
    emitter.sound = assets.uuidOf(handle);
    emitter.handle = handle;
    emitter.spatial = spatial;
    emitter.playOnStart = playOnStart;
    emitter.loop = loop;
    emitter.stream = stream;
    runtime.scene().add<rb::SoundEmitter>(e, emitter);
    return e;
}

// The engine comes up under the null backend (forced via RB_AUDIO_NULL in main), so the rest
// of the suite has a working engine with no real device.
void engineInitsHeadless() {
    rb::AudioSystem audio;
    CHECK(audio.initialized());
    CHECK(audio.activeVoiceCount() == 0u);
}

// A playOnStart emitter becomes a playing voice on the play-begin edge and is freed on play end.
void playBuildsAndStopsVoices(const fs::path& wav) {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AudioSystem audio;
    const rb::Entity e = addEmitter(runtime, assets, wav, glm::vec3(0.0f), false, true);

    audio.onPlayBegin(runtime);
    CHECK(audio.activeVoiceCount() == 1u);
    CHECK(audio.voicePlaying(e));

    audio.onPlayEnd(runtime);
    CHECK(audio.activeVoiceCount() == 0u);
    CHECK(!audio.voicePlaying(e));
}

// playOnStart = false still builds a voice but leaves it stopped until something starts it.
void notStartedWhenPlayOnStartFalse(const fs::path& wav) {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AudioSystem audio;
    const rb::Entity e = addEmitter(runtime, assets, wav, glm::vec3(0.0f), false, false);

    audio.onPlayBegin(runtime);
    CHECK(audio.activeVoiceCount() == 1u);
    CHECK(!audio.voicePlaying(e));
}

// Voices exist only inside a play session: nothing is built before play-begin, and a stray
// update without a session is a no-op.
void gatedBeforePlayBegin(const fs::path& wav) {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AudioSystem audio;
    addEmitter(runtime, assets, wav, glm::vec3(0.0f), false, true);

    audio.onUpdate(runtime, 1.0f / 60.0f);
    CHECK(audio.activeVoiceCount() == 0u);
}

// Spatialization tracks the emitter's flag, and a spatial voice is positioned at the entity's
// world Transform; a non-spatial voice is not spatialized. The listener is taken from the
// active RenderView each update without disturbing the voice.
void spatialFlagAndPosition(const fs::path& wav) {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::RenderView& view = runtime.addResource<rb::RenderView>();
    view.position = glm::vec3(0.0f, 0.0f, 5.0f);
    rb::AudioSystem audio;

    const glm::vec3 spatialPos(3.0f, 1.0f, -2.0f);
    const rb::Entity spatial = addEmitter(runtime, assets, wav, spatialPos, true, true);
    const rb::Entity flat = addEmitter(runtime, assets, wav, glm::vec3(0.0f), false, true);

    audio.onPlayBegin(runtime);
    audio.onUpdate(runtime, 1.0f / 60.0f);

    CHECK(audio.voiceSpatial(spatial));
    CHECK(!audio.voiceSpatial(flat));

    const std::optional<glm::vec3> pos = audio.voicePosition(spatial);
    CHECK(pos.has_value());
    if (pos.has_value()) {
        CHECK(glm::distance(*pos, spatialPos) < 1e-4f);
    }
}

// play()/stop() drive a voice that did not start on its own, and play() re-triggers from the
// beginning after it was stopped. An unknown entity has no voice, so play() reports failure.
void replayWithPlayAndStop(const fs::path& wav) {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AudioSystem audio;
    const rb::Entity e = addEmitter(runtime, assets, wav, glm::vec3(0.0f), false, false);

    audio.onPlayBegin(runtime);
    CHECK(audio.activeVoiceCount() == 1u);
    CHECK(!audio.voicePlaying(e)); // built but idle (playOnStart = false)

    CHECK(audio.play(e));
    CHECK(audio.voicePlaying(e));

    audio.stop(e);
    CHECK(!audio.voicePlaying(e));

    CHECK(audio.play(e)); // re-trigger after stop
    CHECK(audio.voicePlaying(e));

    CHECK(!audio.play(rb::Entity{})); // no voice for an unknown entity
}

// A voice whose entity is destroyed mid-play is reaped on the next update, so a spawn/destroy
// loop cannot leak voices.
void reapsDestroyedEmitter(const fs::path& wav) {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AudioSystem audio;
    const rb::Entity e = addEmitter(runtime, assets, wav, glm::vec3(0.0f), false, true);

    audio.onPlayBegin(runtime);
    CHECK(audio.activeVoiceCount() == 1u);

    runtime.scene().destroy(e);
    audio.onUpdate(runtime, 1.0f / 60.0f);
    CHECK(audio.activeVoiceCount() == 0u);
}

// A streamed emitter (read from disk instead of fully decoded) still builds and plays.
void streamedClipPlays(const fs::path& wav) {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AudioSystem audio;
    const rb::Entity e =
        addEmitter(runtime, assets, wav, glm::vec3(0.0f), false, true, false, /*stream=*/true);

    audio.onPlayBegin(runtime);
    CHECK(audio.activeVoiceCount() == 1u);
    CHECK(audio.voicePlaying(e));
}

// onPlayEnd tears voices down; a fresh onPlayBegin rebuilds them, so a new session plays again.
void rebuildsBetweenSessions(const fs::path& wav) {
    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    rb::AudioSystem audio;
    addEmitter(runtime, assets, wav, glm::vec3(0.0f), false, true);

    audio.onPlayBegin(runtime);
    CHECK(audio.activeVoiceCount() == 1u);
    audio.onPlayEnd(runtime);
    CHECK(audio.activeVoiceCount() == 0u);
    audio.onPlayBegin(runtime);
    CHECK(audio.activeVoiceCount() == 1u);
    audio.onPlayEnd(runtime);
}

// A SoundEmitter that carries only a uuid (as after a scene load) resolves to a live handle:
// the resolve system finds the asset, lazily importing it from the registered source path.
void resolveLazilyImports(const fs::path& wav) {
    rb::AssetManager seed;
    const rb::AssetHandle<rb::AudioAsset> seeded = rb::loadAudioAsset(seed, wav);
    CHECK(seeded.valid());
    const rb::Uuid id = seed.uuidOf(seeded);

    rb::Runtime runtime;
    rb::AssetManager& assets = runtime.addResource<rb::AssetManager>();
    assets.registerSource(id, wav, rb::AssetType::Audio);
    const rb::Entity e = runtime.scene().create();
    rb::SoundEmitter emitter;
    emitter.sound = id; // uuid only; handle starts invalid
    runtime.scene().add<rb::SoundEmitter>(e, emitter);

    rb::AudioAssetResolveSystem resolve;
    resolve.onUpdate(runtime, 0.0f);
    CHECK(runtime.scene().get<rb::SoundEmitter>(e).handle.valid());
}

// SoundEmitter properties survive a scene save/load through the component registry.
void serializeRoundTrip() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Scene scene;
    const rb::Entity e = scene.create();
    rb::SoundEmitter emitter;
    emitter.sound = rb::Uuid::generate();
    emitter.volume = 0.5f;
    emitter.pitch = 1.25f;
    emitter.loop = true;
    emitter.spatial = true;
    emitter.playOnStart = false;
    emitter.stream = true;
    scene.add<rb::SoundEmitter>(e, emitter);

    const nlohmann::json doc = rb::SceneSerializer::toJson(scene, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    bool found = false;
    loaded.each<rb::SoundEmitter>([&](rb::Entity, rb::SoundEmitter& s) {
        found = true;
        CHECK(s.sound == emitter.sound);
        CHECK(s.volume == emitter.volume);
        CHECK(s.pitch == emitter.pitch);
        CHECK(s.loop == emitter.loop);
        CHECK(s.spatial == emitter.spatial);
        CHECK(s.playOnStart == emitter.playOnStart);
        CHECK(s.stream == emitter.stream);
        CHECK(!s.handle.valid()); // a fresh load carries the uuid, not a runtime handle
    });
    CHECK(found);
}

} // namespace

int main() {
    // Force the silent null backend so the suite is deterministic and needs no audio hardware.
    ::setenv("RB_AUDIO_NULL", "1", 1);

    const fs::path root = fs::temp_directory_path() / "rabbet_audio_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    const fs::path wav = root / "blip.wav";
    writeWav(wav);

    engineInitsHeadless();
    playBuildsAndStopsVoices(wav);
    notStartedWhenPlayOnStartFalse(wav);
    gatedBeforePlayBegin(wav);
    spatialFlagAndPosition(wav);
    replayWithPlayAndStop(wav);
    reapsDestroyedEmitter(wav);
    streamedClipPlays(wav);
    rebuildsBetweenSessions(wav);
    resolveLazilyImports(wav);
    serializeRoundTrip();

    fs::remove_all(root, ec);
    return rbtest::summary("audio");
}

#include "rabbet/audio/AudioSystem.h"

#include <algorithm>
#include <cstdlib>
#include <memory>
#include <unordered_map>

#include <glm/glm.hpp>

#include <miniaudio.h>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/audio/AudioAsset.h"
#include "rabbet/audio/SoundEmitter.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/scene/Hierarchy.h"
#include "rabbet/scene/Transform.h"
#include "rabbet/util/Log.h"

namespace rb {
namespace {

ma_bool32 toMaBool(bool value) { return value ? MA_TRUE : MA_FALSE; }

// Decode short clips up front so the voice is ready immediately (deterministic, no decode
// thread); stream long clips from disk so building one can't stall the frame on a full decode.
ma_uint32 soundFlags(const SoundEmitter& emitter) {
    return emitter.stream ? MA_SOUND_FLAG_STREAM : MA_SOUND_FLAG_DECODE;
}

} // namespace

struct AudioSystem::Impl {
    // A miniaudio ma_sound registers nodes that point back at its own address, so it must not
    // be moved after init; each voice is heap-allocated for a stable address across rehashes.
    struct Voice {
        ma_sound sound{};
        ma_audio_buffer buffer{}; // backs an Ogg voice (decoded PCM); unused for file voices
        bool inited = false;
        bool usesBuffer = false;
        // Last values pushed to miniaudio, so update() only calls setters when they change.
        bool hasParams = false;
        float volume = 0.0f;
        float pitch = 0.0f;
        bool loop = false;
        bool spatial = false;
    };

    // Free a voice's miniaudio objects in the right order: the sound reads from the buffer, so
    // it must be uninitialised first.
    static void destroyVoice(Voice& voice) {
        if (voice.inited) {
            ma_sound_uninit(&voice.sound);
        }
        if (voice.usesBuffer) {
            ma_audio_buffer_uninit(&voice.buffer);
        }
    }

    ma_context context{};
    bool contextInited = false;
    ma_engine engine{};
    bool engineOk = false;
    std::unordered_map<Entity, std::unique_ptr<Voice>> voices;
    // The clip uuid that failed to init, per entity: creation retries every update (emitters
    // can appear mid-play), so a failed clip must neither retry nor warn per tick, but
    // assigning a different clip to the same emitter gets a fresh attempt.
    std::unordered_map<Entity, Uuid> failed;
    // Voices exist only inside a play session; the flag keeps a stray update outside one
    // from building any (the scheduler gates this too, but the system defends itself).
    bool inSession = false;

    Impl() {
        const bool forceNull = std::getenv("RB_AUDIO_NULL") != nullptr;
        if (!forceNull && ma_engine_init(nullptr, &engine) == MA_SUCCESS) {
            engineOk = true;
            return;
        }
        // No real device (headless / CI) or forced: fall back to the silent null backend so
        // voice lifecycle still runs. The context must outlive the engine, so we own it.
        const ma_backend backends[] = {ma_backend_null};
        if (ma_context_init(backends, 1, nullptr, &context) == MA_SUCCESS) {
            contextInited = true;
            ma_engine_config config = ma_engine_config_init();
            config.pContext = &context;
            if (ma_engine_init(&config, &engine) == MA_SUCCESS) {
                engineOk = true;
            }
        }
        if (!engineOk) {
            log::warn("audio: no audio engine could be initialised; sound is disabled");
        }
    }

    ~Impl() {
        teardown();
        if (engineOk) {
            ma_engine_uninit(&engine);
        }
        if (contextInited) {
            ma_context_uninit(&context);
        }
    }

    void teardown() {
        for (auto& [entity, voice] : voices) {
            destroyVoice(*voice);
        }
        voices.clear();
        failed.clear();
        inSession = false;
    }

    void applyProperties(Voice& voice, const SoundEmitter& emitter, const glm::vec3* position) {
        // Clamp to sane bounds so a stray serialized/scripted value can't feed miniaudio a
        // negative or absurd gain/pitch. Only push a setter when the value actually changed.
        const float volume = std::clamp(emitter.volume, 0.0f, 16.0f);
        const float pitch = std::clamp(emitter.pitch, 0.0625f, 16.0f);
        if (!voice.hasParams || voice.volume != volume) {
            ma_sound_set_volume(&voice.sound, volume);
            voice.volume = volume;
        }
        if (!voice.hasParams || voice.pitch != pitch) {
            ma_sound_set_pitch(&voice.sound, pitch);
            voice.pitch = pitch;
        }
        if (!voice.hasParams || voice.loop != emitter.loop) {
            ma_sound_set_looping(&voice.sound, toMaBool(emitter.loop));
            voice.loop = emitter.loop;
        }
        if (!voice.hasParams || voice.spatial != emitter.spatial) {
            ma_sound_set_spatialization_enabled(&voice.sound, toMaBool(emitter.spatial));
            voice.spatial = emitter.spatial;
        }
        // Position can change every frame for a moving source, so it is set unconditionally.
        if (emitter.spatial && position != nullptr) {
            ma_sound_set_position(&voice.sound, position->x, position->y, position->z);
        }
        voice.hasParams = true;
    }

    void build(Runtime& runtime) {
        teardown();
        inSession = true;
        if (!engineOk) {
            return;
        }
        AssetManager* assets = runtime.tryResource<AssetManager>();
        if (assets == nullptr) {
            return;
        }
        Scene& scene = runtime.scene();
        scene.each<SoundEmitter>([&](Entity entity, SoundEmitter& emitter) {
            createVoice(scene, *assets, entity, emitter);
        });
    }

    void createVoice(Scene& scene, AssetManager& assets, Entity entity, SoundEmitter& emitter) {
        if (!emitter.handle.valid()) {
            return; // not resolved yet; the resolve system may still deliver it
        }
        const AudioAsset* asset = assets.get<AudioAsset>(emitter.handle);
        if (asset == nullptr || asset->path.empty()) {
            return;
        }
        auto voice = std::make_unique<Voice>();
        ma_result result = MA_ERROR;
        if (!asset->samples.empty() && asset->channels > 0) {
            // Ogg decoded to PCM at import: play from an in-memory buffer (no streaming).
            const ma_uint64 frameCount =
                static_cast<ma_uint64>(asset->samples.size() / asset->channels);
            ma_audio_buffer_config bufferConfig = ma_audio_buffer_config_init(
                ma_format_s16, asset->channels, frameCount, asset->samples.data(), nullptr);
            bufferConfig.sampleRate = asset->sampleRate;
            if (ma_audio_buffer_init(&bufferConfig, &voice->buffer) != MA_SUCCESS) {
                failed[entity] = emitter.sound;
                log::warn("audio: could not buffer clip '{}' for entity {}",
                          asset->path.string(), entity.index());
                return;
            }
            voice->usesBuffer = true;
            result = ma_sound_init_from_data_source(&engine, &voice->buffer, 0, nullptr,
                                                    &voice->sound);
        } else {
            const ma_uint32 flags = soundFlags(emitter);
#ifdef _WIN32
            // Wide overload so non-ASCII asset paths survive the narrowing on Windows.
            result = ma_sound_init_from_file_w(&engine, asset->path.wstring().c_str(), flags,
                                               nullptr, nullptr, &voice->sound);
#else
            result = ma_sound_init_from_file(&engine, asset->path.string().c_str(), flags,
                                             nullptr, nullptr, &voice->sound);
#endif
        }
        if (result != MA_SUCCESS) {
            if (voice->usesBuffer) {
                ma_audio_buffer_uninit(&voice->buffer);
            }
            failed[entity] = emitter.sound;
            log::warn("audio: could not load clip '{}' for entity {}", asset->path.string(),
                      entity.index());
            return;
        }
        voice->inited = true;
        const glm::vec3 world = worldPoseOf(scene, entity).position;
        applyProperties(*voice, emitter,
                        scene.tryGet<Transform>(entity) != nullptr ? &world : nullptr);
        if (emitter.playOnStart && ma_sound_start(&voice->sound) != MA_SUCCESS) {
            log::warn("audio: could not start clip '{}' for entity {}", asset->path.string(),
                      entity.index());
        }
        failed.erase(entity);
        voices.emplace(entity, std::move(voice));
    }

    void updateListener(Runtime& runtime) {
        const RenderView* view = runtime.tryResource<RenderView>();
        if (view == nullptr) {
            return;
        }
        // Camera basis from the inverse view matrix: it looks down -Z, with +Y up.
        const glm::mat4 inv = glm::inverse(view->view);
        const glm::vec3 forward = -glm::vec3(inv[2]);
        const glm::vec3 up = glm::vec3(inv[1]);
        ma_engine_listener_set_position(&engine, 0, view->position.x, view->position.y,
                                        view->position.z);
        ma_engine_listener_set_direction(&engine, 0, forward.x, forward.y, forward.z);
        ma_engine_listener_set_world_up(&engine, 0, up.x, up.y, up.z);
    }

    void reap(Scene& scene) {
        // Drop voices whose emitter was removed or whose entity was destroyed mid-play, so a
        // spawn/destroy gameplay loop can't grow the voice set without bound.
        for (auto it = voices.begin(); it != voices.end();) {
            if (!scene.alive(it->first) || scene.tryGet<SoundEmitter>(it->first) == nullptr) {
                destroyVoice(*it->second);
                it = voices.erase(it);
            } else {
                ++it;
            }
        }
    }

    void update(Runtime& runtime) {
        if (!engineOk) {
            return;
        }
        Scene& scene = runtime.scene();
        reap(scene);
        AssetManager* assets = runtime.tryResource<AssetManager>();
        updateListener(runtime);
        scene.each<SoundEmitter>([&](Entity entity, SoundEmitter& emitter) {
            auto it = voices.find(entity);
            if (it == voices.end()) {
                // An emitter that appeared after the play edge (world.spawn, a scene swap)
                // gets its voice here, playOnStart honored, instead of staying silent.
                if (!inSession || assets == nullptr) {
                    return;
                }
                const auto miss = failed.find(entity);
                if (miss != failed.end() && miss->second == emitter.sound) {
                    return; // this exact clip already failed; retry only on a new assignment
                }
                createVoice(scene, *assets, entity, emitter);
                it = voices.find(entity);
                if (it == voices.end()) {
                    return;
                }
            }
            const glm::vec3 world = worldPoseOf(scene, entity).position;
            applyProperties(*it->second, emitter,
                            scene.tryGet<Transform>(entity) != nullptr ? &world : nullptr);
        });
    }
};

AudioSystem::AudioSystem() : m_impl(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() = default;

void AudioSystem::onUpdate(Runtime& runtime, float) { m_impl->update(runtime); }
void AudioSystem::onPlayBegin(Runtime& runtime) { m_impl->build(runtime); }
void AudioSystem::onPlayEnd(Runtime&) { m_impl->teardown(); }

bool AudioSystem::play(Entity entity) noexcept {
    const auto it = m_impl->voices.find(entity);
    if (it == m_impl->voices.end()) {
        return false;
    }
    ma_sound_seek_to_pcm_frame(&it->second->sound, 0); // replay from the start
    return ma_sound_start(&it->second->sound) == MA_SUCCESS;
}

void AudioSystem::stop(Entity entity) noexcept {
    const auto it = m_impl->voices.find(entity);
    if (it != m_impl->voices.end()) {
        ma_sound_stop(&it->second->sound);
    }
}

bool AudioSystem::initialized() const noexcept { return m_impl->engineOk; }

std::size_t AudioSystem::activeVoiceCount() const noexcept { return m_impl->voices.size(); }

bool AudioSystem::voicePlaying(Entity entity) const noexcept {
    const auto it = m_impl->voices.find(entity);
    if (it == m_impl->voices.end()) {
        return false;
    }
    return ma_sound_is_playing(&it->second->sound) == MA_TRUE;
}

bool AudioSystem::voiceSpatial(Entity entity) const noexcept {
    const auto it = m_impl->voices.find(entity);
    if (it == m_impl->voices.end()) {
        return false;
    }
    return ma_sound_is_spatialization_enabled(&it->second->sound) == MA_TRUE;
}

std::optional<glm::vec3> AudioSystem::voicePosition(Entity entity) const noexcept {
    const auto it = m_impl->voices.find(entity);
    if (it == m_impl->voices.end()) {
        return std::nullopt;
    }
    const ma_vec3f position = ma_sound_get_position(&it->second->sound);
    return glm::vec3(position.x, position.y, position.z);
}

} // namespace rb

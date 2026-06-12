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
#include "rabbet/scene/Transform.h"
#include "rabbet/util/Log.h"

namespace rb {
namespace {

// Decode the whole clip up front (no streaming/async threads) so a voice is fully ready the
// moment it is built — deterministic for tests and fine for the short clips an emitter holds.
constexpr ma_uint32 kSoundFlags = MA_SOUND_FLAG_DECODE;

ma_bool32 toMaBool(bool value) { return value ? MA_TRUE : MA_FALSE; }

} // namespace

struct AudioSystem::Impl {
    // A miniaudio ma_sound registers nodes that point back at its own address, so it must not
    // be moved after init; each voice is heap-allocated for a stable address across rehashes.
    struct Voice {
        ma_sound sound{};
        bool inited = false;
    };

    ma_context context{};
    bool contextInited = false;
    ma_engine engine{};
    bool engineOk = false;
    std::unordered_map<Entity, std::unique_ptr<Voice>> voices;

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
            if (voice->inited) {
                ma_sound_uninit(&voice->sound);
            }
        }
        voices.clear();
    }

    void applyProperties(Voice& voice, const SoundEmitter& emitter, const Transform* transform) {
        ma_sound_set_volume(&voice.sound, std::max(emitter.volume, 0.0f));
        ma_sound_set_pitch(&voice.sound, std::max(emitter.pitch, 0.01f));
        ma_sound_set_looping(&voice.sound, toMaBool(emitter.loop));
        ma_sound_set_spatialization_enabled(&voice.sound, toMaBool(emitter.spatial));
        if (emitter.spatial && transform != nullptr) {
            ma_sound_set_position(&voice.sound, transform->position.x, transform->position.y,
                                  transform->position.z);
        }
    }

    void build(Runtime& runtime) {
        teardown();
        if (!engineOk) {
            return;
        }
        AssetManager* assets = runtime.tryResource<AssetManager>();
        if (assets == nullptr) {
            return;
        }
        Scene& scene = runtime.scene();
        scene.each<SoundEmitter>([&](Entity entity, SoundEmitter& emitter) {
            if (!emitter.handle.valid()) {
                return;
            }
            const AudioAsset* asset = assets->get<AudioAsset>(emitter.handle);
            if (asset == nullptr || asset->path.empty()) {
                return;
            }
            auto voice = std::make_unique<Voice>();
            const ma_result result = ma_sound_init_from_file(
                &engine, asset->path.string().c_str(), kSoundFlags, nullptr, nullptr, &voice->sound);
            if (result != MA_SUCCESS) {
                log::warn("audio: could not load clip '{}' for entity {}", asset->path.string(),
                          entity.index());
                return;
            }
            voice->inited = true;
            applyProperties(*voice, emitter, scene.tryGet<Transform>(entity));
            if (emitter.playOnStart) {
                ma_sound_start(&voice->sound);
            }
            voices.emplace(entity, std::move(voice));
        });
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

    void update(Runtime& runtime) {
        if (!engineOk || voices.empty()) {
            return;
        }
        updateListener(runtime);
        Scene& scene = runtime.scene();
        scene.each<SoundEmitter>([&](Entity entity, SoundEmitter& emitter) {
            const auto it = voices.find(entity);
            if (it == voices.end()) {
                return;
            }
            applyProperties(*it->second, emitter, scene.tryGet<Transform>(entity));
        });
    }
};

AudioSystem::AudioSystem() : m_impl(std::make_unique<Impl>()) {}
AudioSystem::~AudioSystem() = default;

void AudioSystem::onUpdate(Runtime& runtime, float) { m_impl->update(runtime); }
void AudioSystem::onPlayBegin(Runtime& runtime) { m_impl->build(runtime); }
void AudioSystem::onPlayEnd(Runtime&) { m_impl->teardown(); }

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

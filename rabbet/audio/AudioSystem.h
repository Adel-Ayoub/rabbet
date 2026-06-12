#pragma once

#include <cstddef>
#include <memory>
#include <optional>

#include <glm/glm.hpp>

#include "rabbet/core/System.h"
#include "rabbet/ecs/Entity.h"

namespace rb {

// Drives a miniaudio engine during Play. On the play-session begin edge it creates a voice
// for every entity with a SoundEmitter whose clip resolved, applying volume/pitch/loop and
// (for spatial emitters) positioning them at their Transform; each tick it reconciles those
// properties with live inspector edits and updates the listener from the active camera
// (the RenderView resource); on play end it stops and frees the voices. miniaudio lives
// behind the impl, so this header — and the rest of the engine and editor — never include it.
//
// If no real playback device is available (headless / CI) the engine falls back to
// miniaudio's null backend, so voice lifecycle still runs deterministically without sound;
// setting RB_AUDIO_NULL forces that path.
class AudioSystem final : public System {
public:
    AudioSystem();
    ~AudioSystem() override;

    AudioSystem(const AudioSystem&) = delete;
    AudioSystem& operator=(const AudioSystem&) = delete;
    AudioSystem(AudioSystem&&) = delete;
    AudioSystem& operator=(AudioSystem&&) = delete;

    void onUpdate(Runtime& runtime, float dt) override;
    void onPlayBegin(Runtime& runtime) override;
    void onPlayEnd(Runtime& runtime) override;

    // Observation seams for headless tests (no audio output is asserted, only wiring).
    [[nodiscard]] bool initialized() const noexcept;
    [[nodiscard]] std::size_t activeVoiceCount() const noexcept;
    [[nodiscard]] bool voicePlaying(Entity entity) const noexcept;
    [[nodiscard]] bool voiceSpatial(Entity entity) const noexcept;
    [[nodiscard]] std::optional<glm::vec3> voicePosition(Entity entity) const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace rb

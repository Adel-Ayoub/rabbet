#pragma once

#include <array>
#include <optional>

#include "rabbet/core/System.h"
#include "rabbet/core/Uuid.h"
#include "rabbet/render/gl/Shader.h"

namespace rb {

class SkyboxSystem final : public System {
public:
    void onStart(Runtime& runtime) override;
    void onUpdate(Runtime& runtime, float dt) override;

private:
    // Rebuilds the Skybox resource when the wanted face set differs from the uploaded one.
    // A scene with no SkyboxComponent falls back to the SkyboxDefault resource, so a
    // scene's sky depends on the scene rather than on what happened to be loaded before.
    // NOTE: this replaces the cubemap the RenderSystem samples, so register SkyboxSystem
    // BEFORE RenderSystem or a swap frame samples a deleted texture.
    void reconcile(Runtime& runtime);

    std::optional<gl::Shader> m_shader;
    std::array<Uuid, 6> m_loaded{}; // face uuids behind the live cubemap
    std::array<Uuid, 6> m_warned{}; // last set warned about, so a broken sky logs once
};

} // namespace rb

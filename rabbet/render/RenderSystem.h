#pragma once

#include <optional>

#include "rabbet/core/System.h"
#include "rabbet/render/gl/Shader.h"

namespace rb {

class RenderSystem final : public System {
public:
    void onStart(Runtime& runtime) override;
    void onUpdate(Runtime& runtime, float dt) override;

private:
    std::optional<gl::Shader> m_phong;
    std::optional<gl::Shader> m_pbr;
};

} // namespace rb

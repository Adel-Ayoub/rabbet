#pragma once

#include "rabbet/render/RenderDevice.h"

namespace rb::gl {

class Device final : public RenderDevice {
public:
    void setViewport(int width, int height) override;
    void setClearColor(const glm::vec4& color) override;
    void clear() override;
    void setDepthTest(bool enabled) override;
    void setCullFace(bool enabled) override;

    [[nodiscard]] std::string_view backendName() const override;
};

} // namespace rb::gl

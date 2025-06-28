#pragma once

#include <memory>
#include <string_view>

#include <glm/glm.hpp>

namespace rb {

class RenderDevice {
public:
    RenderDevice() = default;
    RenderDevice(const RenderDevice&) = delete;
    RenderDevice& operator=(const RenderDevice&) = delete;
    RenderDevice(RenderDevice&&) = delete;
    RenderDevice& operator=(RenderDevice&&) = delete;
    virtual ~RenderDevice() = default;

    virtual void setViewport(int width, int height) = 0;
    virtual void setClearColor(const glm::vec4& color) = 0;
    virtual void clear() = 0;
    virtual void setDepthTest(bool enabled) = 0;
    virtual void setCullFace(bool enabled) = 0;

    [[nodiscard]] virtual std::string_view backendName() const = 0;
};

[[nodiscard]] std::unique_ptr<RenderDevice> createRenderDevice();

} // namespace rb

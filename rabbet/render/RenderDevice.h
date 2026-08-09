#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include <glm/glm.hpp>

namespace rb {

class Window;
enum class WindowClientApi;

enum class RendererBackend { Auto, OpenGL, Vulkan };

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
    virtual void present() = 0;

    [[nodiscard]] virtual std::string_view backendName() const = 0;
};

[[nodiscard]] std::optional<RendererBackend>
rendererBackendFromName(std::string_view name) noexcept;
[[nodiscard]] RendererBackend resolveRendererBackend(RendererBackend backend) noexcept;
[[nodiscard]] WindowClientApi windowClientApiForRenderer(RendererBackend backend) noexcept;
[[nodiscard]] std::unique_ptr<RenderDevice> createRenderDevice(RendererBackend backend,
                                                               Window& window);

} // namespace rb

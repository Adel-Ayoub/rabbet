#pragma once

#include "rabbet/render/RenderDevice.h"

struct GLFWwindow;

namespace rb::gl {

class Device final : public RenderDevice {
public:
    [[nodiscard]] static std::unique_ptr<Device> create(const Window& window);

    void setViewport(int width, int height) override;
    void setClearColor(const glm::vec4& color) override;
    void clear() override;
    void setDepthTest(bool enabled) override;
    void setCullFace(bool enabled) override;
    void present() override;

    [[nodiscard]] std::string_view backendName() const override;

private:
    explicit Device(std::shared_ptr<GLFWwindow> window) noexcept;
    void makeCurrent() const noexcept;

    std::shared_ptr<GLFWwindow> m_window;
};

} // namespace rb::gl

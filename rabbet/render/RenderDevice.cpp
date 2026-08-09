#include "rabbet/render/RenderDevice.h"

#include "rabbet/platform/Window.h"
#include "rabbet/render/gl/Device.h"
#include "rabbet/util/Log.h"

namespace rb {

std::optional<RendererBackend> rendererBackendFromName(std::string_view name) noexcept {
    if (name == "auto") {
        return RendererBackend::Auto;
    }
    if (name == "opengl") {
        return RendererBackend::OpenGL;
    }
    if (name == "vulkan") {
        return RendererBackend::Vulkan;
    }
    return std::nullopt;
}

RendererBackend resolveRendererBackend(RendererBackend backend) noexcept {
    return backend == RendererBackend::Auto ? RendererBackend::OpenGL : backend;
}

WindowClientApi windowClientApiForRenderer(RendererBackend backend) noexcept {
    switch (resolveRendererBackend(backend)) {
    case RendererBackend::Vulkan:
        return WindowClientApi::None;
    case RendererBackend::Auto:
    case RendererBackend::OpenGL:
        return WindowClientApi::OpenGL;
    }
    return WindowClientApi::OpenGL;
}

std::unique_ptr<RenderDevice> createRenderDevice(RendererBackend backend, Window& window) {
    if (window.handle() == nullptr) {
        log::error("render device creation needs a live window");
        return nullptr;
    }

    const RendererBackend resolved = resolveRendererBackend(backend);
    switch (resolved) {
    case RendererBackend::OpenGL:
        return gl::Device::create(window);
    case RendererBackend::Vulkan:
        if (window.clientApi() != WindowClientApi::None) {
            log::error("window client API does not match the selected renderer");
            return nullptr;
        }
        log::error("Vulkan scene rendering is not available yet");
        return nullptr;
    case RendererBackend::Auto:
        break;
    }

    log::error("renderer selection is invalid");
    return nullptr;
}

} // namespace rb

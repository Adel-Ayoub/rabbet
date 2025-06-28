#include "rabbet/render/RenderDevice.h"

#include "rabbet/render/gl/Device.h"

namespace rb {

std::unique_ptr<RenderDevice> createRenderDevice() {
    return std::make_unique<gl::Device>();
}

} // namespace rb

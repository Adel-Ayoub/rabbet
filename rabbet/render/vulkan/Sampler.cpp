#include "rabbet/render/vulkan/Sampler.h"

#include "rabbet/render/vulkan/Device.h"

#include <cstdio>

namespace rb::vulkan {

std::unique_ptr<Sampler> Sampler::create(const Device& device,
                                         const SamplerDescription& description) {
    VkSamplerCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    createInfo.magFilter = description.magFilter;
    createInfo.minFilter = description.minFilter;
    createInfo.mipmapMode = description.mipmapMode;
    createInfo.addressModeU = description.addressMode;
    createInfo.addressModeV = description.addressMode;
    createInfo.addressModeW = description.addressMode;
    createInfo.maxLod = description.maxLod;
    createInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    VkSampler sampler = VK_NULL_HANDLE;
    if (vkCreateSampler(device.handle(), &createInfo, nullptr, &sampler) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan sampler creation failed\n");
        return nullptr;
    }
    return std::unique_ptr<Sampler>(new Sampler(device.handle(), sampler));
}

Sampler::Sampler(VkDevice device, VkSampler sampler) noexcept
    : m_device(device), m_sampler(sampler) {}

Sampler::~Sampler() {
    if (m_sampler != VK_NULL_HANDLE) {
        vkDestroySampler(m_device, m_sampler, nullptr);
    }
}

VkSampler Sampler::handle() const noexcept {
    return m_sampler;
}

}

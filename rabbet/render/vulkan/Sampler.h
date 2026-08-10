#pragma once

#include <vulkan/vulkan.h>

#include <memory>

namespace rb::vulkan {

class Device;

struct SamplerDescription {
    VkFilter magFilter{VK_FILTER_LINEAR};
    VkFilter minFilter{VK_FILTER_LINEAR};
    VkSamplerMipmapMode mipmapMode{VK_SAMPLER_MIPMAP_MODE_LINEAR};
    VkSamplerAddressMode addressMode{VK_SAMPLER_ADDRESS_MODE_REPEAT};
    float maxLod{VK_LOD_CLAMP_NONE};
};

class Sampler {
public:
    static std::unique_ptr<Sampler> create(const Device& device,
                                           const SamplerDescription& description);

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    ~Sampler();

    [[nodiscard]] VkSampler handle() const noexcept;

private:
    Sampler(VkDevice device, VkSampler sampler) noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
};

}

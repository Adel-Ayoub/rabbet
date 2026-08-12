#pragma once

#include "rabbet/render/TextureConfig.h"

#include <vulkan/vulkan.h>

#include <concepts>
#include <cstdint>
#include <memory>
#include <type_traits>

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
    template <typename Config>
        requires std::same_as<std::remove_cvref_t<Config>, SamplerConfig>
    static std::unique_ptr<Sampler> create(const Device& device, Config&& config,
                                           std::uint32_t mipLevels = 1) {
        return createNeutral(device, config, mipLevels);
    }
    static std::unique_ptr<Sampler> create(const Device& device,
                                           const SamplerDescription& description);

    Sampler(const Sampler&) = delete;
    Sampler& operator=(const Sampler&) = delete;
    ~Sampler();

    [[nodiscard]] VkSampler handle() const noexcept;

private:
    static std::unique_ptr<Sampler> createNeutral(const Device& device,
                                                  const SamplerConfig& config,
                                                  std::uint32_t mipLevels);
    Sampler(VkDevice device, VkSampler sampler) noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    VkSampler m_sampler{VK_NULL_HANDLE};
};

}

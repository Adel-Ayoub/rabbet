#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <span>

namespace rb::vulkan {

class Device;

inline constexpr std::uint32_t perFrameSetIndex = 0;
inline constexpr std::uint32_t perDrawSetIndex = 1;
inline constexpr std::uint32_t maxDescriptorSetCount = 4;
inline constexpr std::uint32_t maxPushConstantBytes = 128;

class DescriptorSetLayout {
public:
    static std::unique_ptr<DescriptorSetLayout> create(
        const Device& device, std::span<const VkDescriptorSetLayoutBinding> bindings);

    DescriptorSetLayout(const DescriptorSetLayout&) = delete;
    DescriptorSetLayout& operator=(const DescriptorSetLayout&) = delete;
    ~DescriptorSetLayout();

    [[nodiscard]] VkDescriptorSetLayout handle() const noexcept;

private:
    DescriptorSetLayout(VkDevice device, VkDescriptorSetLayout layout) noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    VkDescriptorSetLayout m_layout{VK_NULL_HANDLE};
};

class DescriptorPool {
public:
    static std::unique_ptr<DescriptorPool> create(const Device& device, std::uint32_t maxSets,
                                                  std::span<const VkDescriptorPoolSize> sizes);

    DescriptorPool(const DescriptorPool&) = delete;
    DescriptorPool& operator=(const DescriptorPool&) = delete;
    ~DescriptorPool();

    [[nodiscard]] VkDescriptorSet allocate(VkDescriptorSetLayout layout);
    [[nodiscard]] bool reset() noexcept;
    [[nodiscard]] VkDescriptorPool handle() const noexcept;

private:
    DescriptorPool(VkDevice device, VkDescriptorPool pool) noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    VkDescriptorPool m_pool{VK_NULL_HANDLE};
};

}

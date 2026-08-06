#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>

namespace rb::vulkan {

class Instance;

struct DeviceLimits {
    std::uint32_t pushConstantBytes{0};
    std::uint32_t descriptorSetCount{0};
    VkDeviceSize uniformBufferOffsetAlignment{0};
};

class Device {
public:
    static std::unique_ptr<Device> create(const Instance& instance);

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    ~Device();

    [[nodiscard]] VkPhysicalDevice physicalHandle() const noexcept;
    [[nodiscard]] VkDevice handle() const noexcept;
    [[nodiscard]] VkQueue graphicsQueue() const noexcept;
    [[nodiscard]] VkQueue presentQueue() const noexcept;
    [[nodiscard]] std::uint32_t graphicsQueueFamily() const noexcept;
    [[nodiscard]] std::uint32_t presentQueueFamily() const noexcept;
    [[nodiscard]] const VkPhysicalDeviceProperties& properties() const noexcept;
    [[nodiscard]] const DeviceLimits& limits() const noexcept;
    [[nodiscard]] bool waitIdle() const noexcept;

private:
    Device(VkPhysicalDevice physicalDevice, VkDevice device, VkQueue graphicsQueue,
           VkQueue presentQueue, std::uint32_t graphicsQueueFamily,
           std::uint32_t presentQueueFamily, const VkPhysicalDeviceProperties& properties) noexcept;

    VkPhysicalDevice m_physicalDevice{VK_NULL_HANDLE};
    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_graphicsQueue{VK_NULL_HANDLE};
    VkQueue m_presentQueue{VK_NULL_HANDLE};
    std::uint32_t m_graphicsQueueFamily{0};
    std::uint32_t m_presentQueueFamily{0};
    VkPhysicalDeviceProperties m_properties{};
    DeviceLimits m_limits{};
};

}

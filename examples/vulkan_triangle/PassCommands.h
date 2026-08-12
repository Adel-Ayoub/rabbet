#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <iostream>

// One transient command buffer with a wait fence, shared by the probe's offscreen
// passes. The label names the owning pass in every failure line.
struct PassCommands {
    VkDevice device{VK_NULL_HANDLE};
    VkCommandPool pool{VK_NULL_HANDLE};
    VkCommandBuffer buffer{VK_NULL_HANDLE};
    VkFence fence{VK_NULL_HANDLE};
    const char* label{"pass"};
    std::uint64_t timeoutNanoseconds{5'000'000'000ULL};

    PassCommands() = default;
    PassCommands(const PassCommands&) = delete;
    PassCommands& operator=(const PassCommands&) = delete;

    ~PassCommands() {
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (pool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, pool, nullptr);
        }
    }

    [[nodiscard]] bool create(VkDevice createDevice, std::uint32_t queueFamily,
                              const char* passLabel) {
        device = createDevice;
        label = passLabel;
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT |
                         VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = queueFamily;
        if (vkCreateCommandPool(device, &poolInfo, nullptr, &pool) != VK_SUCCESS) {
            std::cerr << "Vulkan " << label << " command pool creation failed\n";
            return false;
        }
        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = pool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        if (vkAllocateCommandBuffers(device, &allocateInfo, &buffer) != VK_SUCCESS) {
            std::cerr << "Vulkan " << label << " command buffer allocation failed\n";
            return false;
        }
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (vkCreateFence(device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
            std::cerr << "Vulkan " << label << " fence creation failed\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool begin() {
        if (vkResetCommandBuffer(buffer, 0) != VK_SUCCESS) {
            std::cerr << "Vulkan " << label << " command buffer reset failed\n";
            return false;
        }
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(buffer, &beginInfo) != VK_SUCCESS) {
            std::cerr << "Vulkan " << label << " command buffer begin failed\n";
            return false;
        }
        return true;
    }

    [[nodiscard]] bool submitAndWait(VkQueue queue) {
        if (vkEndCommandBuffer(buffer) != VK_SUCCESS) {
            std::cerr << "Vulkan " << label << " command buffer end failed\n";
            return false;
        }
        if (vkResetFences(device, 1, &fence) != VK_SUCCESS) {
            std::cerr << "Vulkan " << label << " fence reset failed\n";
            return false;
        }
        VkCommandBufferSubmitInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandInfo.commandBuffer = buffer;
        VkSubmitInfo2 submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submitInfo.commandBufferInfoCount = 1;
        submitInfo.pCommandBufferInfos = &commandInfo;
        if (vkQueueSubmit2(queue, 1, &submitInfo, fence) != VK_SUCCESS) {
            std::cerr << "Vulkan " << label << " submission failed\n";
            return false;
        }
        if (vkWaitForFences(device, 1, &fence, VK_TRUE, timeoutNanoseconds) != VK_SUCCESS) {
            std::cerr << "Vulkan " << label << " fence wait failed\n";
            static_cast<void>(vkDeviceWaitIdle(device));
            return false;
        }
        return true;
    }
};

#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <span>

namespace rb::vulkan {

struct ImageBarrier {
    VkImage image{VK_NULL_HANDLE};
    VkPipelineStageFlags2 srcStage{VK_PIPELINE_STAGE_2_NONE};
    VkAccessFlags2 srcAccess{VK_ACCESS_2_NONE};
    VkPipelineStageFlags2 dstStage{VK_PIPELINE_STAGE_2_NONE};
    VkAccessFlags2 dstAccess{VK_ACCESS_2_NONE};
    VkImageLayout oldLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageLayout newLayout{VK_IMAGE_LAYOUT_UNDEFINED};
    VkImageAspectFlags aspect{VK_IMAGE_ASPECT_COLOR_BIT};
    std::uint32_t baseMipLevel{0};
    std::uint32_t mipLevels{1};
    std::uint32_t baseArrayLayer{0};
    std::uint32_t arrayLayers{1};
};

struct BufferBarrier {
    VkBuffer buffer{VK_NULL_HANDLE};
    VkPipelineStageFlags2 srcStage{VK_PIPELINE_STAGE_2_NONE};
    VkAccessFlags2 srcAccess{VK_ACCESS_2_NONE};
    VkPipelineStageFlags2 dstStage{VK_PIPELINE_STAGE_2_NONE};
    VkAccessFlags2 dstAccess{VK_ACCESS_2_NONE};
    VkDeviceSize offset{0};
    VkDeviceSize size{VK_WHOLE_SIZE};
};

void cmdBarriers(VkCommandBuffer commandBuffer, std::span<const ImageBarrier> imageBarriers,
                 std::span<const BufferBarrier> bufferBarriers);
void cmdImageBarrier(VkCommandBuffer commandBuffer, const ImageBarrier& barrier);
void cmdBufferBarrier(VkCommandBuffer commandBuffer, const BufferBarrier& barrier);

}

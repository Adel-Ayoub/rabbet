#pragma once

#include "rabbet/render/vulkan/Image.h"

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

namespace rb::vulkan {

class Allocator;
class Device;
class Readback;

struct OffscreenTargetDescription {
    VkExtent2D extent{};
    VkFormat colorFormat{VK_FORMAT_UNDEFINED};
    bool depth{false};
    bool sampledColor{false};
    bool sampledDepth{false};
};

class OffscreenTarget {
public:
    static constexpr std::int32_t noPickId = -1;

    static std::unique_ptr<OffscreenTarget> create(
        const Device& device, Allocator& allocator,
        const OffscreenTargetDescription& description);

    OffscreenTarget(const OffscreenTarget&) = delete;
    OffscreenTarget& operator=(const OffscreenTarget&) = delete;
    ~OffscreenTarget() = default;

    [[nodiscard]] Image* color() noexcept;
    [[nodiscard]] const Image* color() const noexcept;
    [[nodiscard]] Image* depth() noexcept;
    [[nodiscard]] const Image* depth() const noexcept;
    [[nodiscard]] VkExtent2D extent() const noexcept;

    [[nodiscard]] bool readColor(Readback& readback, VkImageLayout currentLayout,
                                 VkPipelineStageFlags2 currentStage,
                                 VkAccessFlags2 currentAccess,
                                 std::span<std::byte> destination) const;
    [[nodiscard]] bool readDepth(Readback& readback, VkImageLayout currentLayout,
                                 VkPipelineStageFlags2 currentStage,
                                 VkAccessFlags2 currentAccess,
                                 std::span<std::byte> destination) const;
    [[nodiscard]] bool readPickId(Readback& readback, std::int32_t x, std::int32_t y,
                                  VkImageLayout currentLayout,
                                  VkPipelineStageFlags2 currentStage,
                                  VkAccessFlags2 currentAccess,
                                  std::int32_t& value) const;

private:
    OffscreenTarget(VkExtent2D extent, std::unique_ptr<Image> color,
                    std::unique_ptr<Image> depth) noexcept;

    VkExtent2D m_extent{};
    std::unique_ptr<Image> m_color;
    std::unique_ptr<Image> m_depth;
};

}

#pragma once

#include <vulkan/vulkan.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace rb::vulkan {

class Device;
class Swapchain;

enum class FrameResult {
    rendered,
    idle,
    needsRecreate,
    deviceLost,
    fatal,
};

enum class DeviceLossSite {
    none,
    submit,
    present,
};

enum class SwapchainRecreateResult {
    complete,
    unavailable,
    fatal,
};

class FrameLoop {
public:
    static std::unique_ptr<FrameLoop> create(const Device& device, const Swapchain& swapchain,
                                             std::span<const std::uint32_t> vertexCode,
                                             std::span<const std::uint32_t> fragmentCode);

    FrameLoop(const FrameLoop&) = delete;
    FrameLoop& operator=(const FrameLoop&) = delete;
    ~FrameLoop();

    // After a fatal or deviceLost result every later call returns that result again
    // without touching the device.
    [[nodiscard]] FrameResult draw(const Swapchain& swapchain);
    [[nodiscard]] SwapchainRecreateResult recreateSwapchain(Swapchain& swapchain,
                                                            VkExtent2D extent);
    [[nodiscard]] bool waitIdle() noexcept;
    [[nodiscard]] std::uint64_t frameCount() const noexcept;

    // The next draw reaching the named site behaves as if the device were lost there,
    // after its real work completed, so teardown still finds a healthy device.
    void simulateDeviceLoss(DeviceLossSite site) noexcept;
    [[nodiscard]] bool deviceLost() const noexcept;

private:
    static constexpr std::size_t frameSlotCount = 2;

    struct FrameSlot {
        VkCommandBuffer commandBuffer{VK_NULL_HANDLE};
        VkSemaphore imageAvailable{VK_NULL_HANDLE};
        VkFence fence{VK_NULL_HANDLE};
    };

    FrameLoop(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
              std::uint32_t queueFamily, std::span<const std::uint32_t> vertexCode,
              std::span<const std::uint32_t> fragmentCode);

    [[nodiscard]] FrameResult drawFrame(const Swapchain& swapchain);
    [[nodiscard]] bool createFrameResources();
    [[nodiscard]] bool rebuild(const Swapchain& swapchain);
    [[nodiscard]] bool createPipeline(VkFormat format, VkPipelineLayout& layout,
                                      VkPipeline& pipeline) const;
    [[nodiscard]] bool createPresentResources(std::size_t count,
                                              std::vector<VkSemaphore>& semaphores,
                                              std::vector<VkFence>& fences) const;
    [[nodiscard]] bool recordCommands(VkCommandBuffer commandBuffer, const Swapchain& swapchain,
                                      std::uint32_t imageIndex) const;
    [[nodiscard]] bool waitForPresentations() noexcept;
    void reportDeviceLoss(const char* site) noexcept;
    void consumeAcquiredImage(VkSemaphore imageAvailable) noexcept;
    void destroy() noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    VkQueue m_graphicsQueue{VK_NULL_HANDLE};
    VkQueue m_presentQueue{VK_NULL_HANDLE};
    std::uint32_t m_queueFamily{0};
    VkCommandPool m_commandPool{VK_NULL_HANDLE};
    std::array<FrameSlot, frameSlotCount> m_frames{};
    std::vector<VkSemaphore> m_renderFinished;
    std::vector<VkFence> m_presentFences;
    std::vector<std::uint8_t> m_presentPending;
    std::vector<VkFence> m_imageFences;
    std::vector<std::uint32_t> m_vertexCode;
    std::vector<std::uint32_t> m_fragmentCode;
    VkPipelineLayout m_pipelineLayout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
    std::size_t m_currentFrame{0};
    std::uint64_t m_frameCount{0};
    DeviceLossSite m_simulatedLoss{DeviceLossSite::none};
    bool m_deviceLost{false};
    bool m_fatal{false};
};

}

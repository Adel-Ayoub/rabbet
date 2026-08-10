#include "rabbet/render/vulkan/FrameLoop.h"

#include "rabbet/render/vulkan/Device.h"
#include "rabbet/render/vulkan/Swapchain.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <utility>

namespace rb::vulkan {

namespace {

VkShaderModule createShaderModule(VkDevice device, const std::vector<std::uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(std::uint32_t);
    createInfo.pCode = code.data();

    VkShaderModule module = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &module) != VK_SUCCESS) {
        return VK_NULL_HANDLE;
    }
    return module;
}

void destroySemaphores(VkDevice device, const std::vector<VkSemaphore>& semaphores) {
    for (const VkSemaphore semaphore : semaphores) {
        vkDestroySemaphore(device, semaphore, nullptr);
    }
}

void destroyFences(VkDevice device, const std::vector<VkFence>& fences) {
    for (const VkFence fence : fences) {
        vkDestroyFence(device, fence, nullptr);
    }
}

}

std::unique_ptr<FrameLoop> FrameLoop::create(const Device& device, const Swapchain& swapchain,
                                             std::span<const std::uint32_t> vertexCode,
                                             std::span<const std::uint32_t> fragmentCode) {
    if (vertexCode.empty() || fragmentCode.empty()) {
        std::fprintf(stderr, "Vulkan shader code is empty\n");
        return nullptr;
    }

    auto frameLoop = std::unique_ptr<FrameLoop>(
        new FrameLoop(device.handle(), device.graphicsQueue(), device.presentQueue(),
                      device.graphicsQueueFamily(), vertexCode, fragmentCode));
    if (!frameLoop->createFrameResources() || !frameLoop->rebuild(swapchain)) {
        return nullptr;
    }
    return frameLoop;
}

FrameLoop::FrameLoop(VkDevice device, VkQueue graphicsQueue, VkQueue presentQueue,
                     std::uint32_t queueFamily, std::span<const std::uint32_t> vertexCode,
                     std::span<const std::uint32_t> fragmentCode)
    : m_device(device), m_graphicsQueue(graphicsQueue), m_presentQueue(presentQueue),
      m_queueFamily(queueFamily), m_vertexCode(vertexCode.begin(), vertexCode.end()),
      m_fragmentCode(fragmentCode.begin(), fragmentCode.end()) {}

FrameLoop::~FrameLoop() {
    static_cast<void>(waitIdle());
    destroy();
}

bool FrameLoop::createFrameResources() {
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = m_queueFamily;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    if (vkCreateCommandPool(m_device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan command pool creation failed\n");
        return false;
    }

    std::array<VkCommandBuffer, frameSlotCount> commandBuffers{};
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = static_cast<std::uint32_t>(commandBuffers.size());
    if (vkAllocateCommandBuffers(m_device, &allocateInfo, commandBuffers.data()) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan command buffer allocation failed\n");
        vkDestroyCommandPool(m_device, commandPool, nullptr);
        return false;
    }

    std::array<FrameSlot, frameSlotCount> frames{};
    const auto destroyCreatedFrames = [this, &frames]() {
        for (const FrameSlot& frame : frames) {
            if (frame.imageAvailable != VK_NULL_HANDLE) {
                vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
            }
            if (frame.fence != VK_NULL_HANDLE) {
                vkDestroyFence(m_device, frame.fence, nullptr);
            }
        }
    };
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::size_t index = 0; index < frames.size(); ++index) {
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &imageAvailable) != VK_SUCCESS) {
            std::fprintf(stderr, "Vulkan frame synchronization creation failed\n");
            destroyCreatedFrames();
            vkDestroyCommandPool(m_device, commandPool, nullptr);
            return false;
        }
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
            std::fprintf(stderr, "Vulkan frame synchronization creation failed\n");
            vkDestroySemaphore(m_device, imageAvailable, nullptr);
            destroyCreatedFrames();
            vkDestroyCommandPool(m_device, commandPool, nullptr);
            return false;
        }
        frames[index] = FrameSlot{commandBuffers[index], imageAvailable, fence};
    }
    m_commandPool = commandPool;
    m_frames = frames;
    return true;
}

bool FrameLoop::createPipeline(VkFormat format, VkPipelineLayout& layout,
                               VkPipeline& pipeline) const {
    const VkShaderModule vertexModule = createShaderModule(m_device, m_vertexCode);
    const VkShaderModule fragmentModule = createShaderModule(m_device, m_fragmentCode);
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Vulkan shader module creation failed\n");
        if (vertexModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, vertexModule, nullptr);
        }
        if (fragmentModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_device, fragmentModule, nullptr);
        }
        return false;
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    if (vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan pipeline layout creation failed\n");
        vkDestroyShaderModule(m_device, fragmentModule, nullptr);
        vkDestroyShaderModule(m_device, vertexModule, nullptr);
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> shaderStages{};
    shaderStages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStages[0].module = vertexModule;
    shaderStages[0].pName = "main";
    shaderStages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStages[1].module = fragmentModule;
    shaderStages[1].pName = "main";
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = VK_CULL_MODE_NONE;
    rasterization.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterization.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState colorAttachment{};
    colorAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = 1;
    colorBlend.pAttachments = &colorAttachment;
    constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &format;

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext = &renderingInfo;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterization;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;

    const VkResult pipelineResult =
        vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(m_device, fragmentModule, nullptr);
    vkDestroyShaderModule(m_device, vertexModule, nullptr);
    if (pipelineResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan graphics pipeline creation failed with result %d\n",
                     static_cast<int>(pipelineResult));
        vkDestroyPipelineLayout(m_device, layout, nullptr);
        layout = VK_NULL_HANDLE;
        return false;
    }
    return true;
}

bool FrameLoop::createPresentResources(std::size_t count, std::vector<VkSemaphore>& semaphores,
                                       std::vector<VkFence>& fences) const {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    semaphores.reserve(count);
    fences.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        VkSemaphore semaphore = VK_NULL_HANDLE;
        if (vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            std::fprintf(stderr, "Vulkan presentation resource creation failed\n");
            destroySemaphores(m_device, semaphores);
            destroyFences(m_device, fences);
            semaphores.clear();
            fences.clear();
            return false;
        }
        VkFence fence = VK_NULL_HANDLE;
        if (vkCreateFence(m_device, &fenceInfo, nullptr, &fence) != VK_SUCCESS) {
            std::fprintf(stderr, "Vulkan presentation resource creation failed\n");
            vkDestroySemaphore(m_device, semaphore, nullptr);
            destroySemaphores(m_device, semaphores);
            destroyFences(m_device, fences);
            semaphores.clear();
            fences.clear();
            return false;
        }
        semaphores.push_back(semaphore);
        fences.push_back(fence);
    }
    return true;
}

bool FrameLoop::rebuild(const Swapchain& swapchain) {
    if (!waitForPresentations()) {
        return false;
    }
    VkPipelineLayout newLayout = VK_NULL_HANDLE;
    VkPipeline newPipeline = VK_NULL_HANDLE;
    if (!createPipeline(swapchain.format(), newLayout, newPipeline)) {
        return false;
    }

    std::vector<VkSemaphore> newRenderFinished;
    std::vector<VkFence> newPresentFences;
    if (!createPresentResources(swapchain.images().size(), newRenderFinished, newPresentFences)) {
        vkDestroyPipeline(m_device, newPipeline, nullptr);
        vkDestroyPipelineLayout(m_device, newLayout, nullptr);
        return false;
    }

    destroySemaphores(m_device, m_renderFinished);
    destroyFences(m_device, m_presentFences);
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
    }
    m_renderFinished = std::move(newRenderFinished);
    m_presentFences = std::move(newPresentFences);
    m_presentPending.assign(swapchain.images().size(), 0U);
    m_imageFences.assign(swapchain.images().size(), VK_NULL_HANDLE);
    m_pipelineLayout = newLayout;
    m_pipeline = newPipeline;
    return true;
}

SwapchainRecreateResult FrameLoop::recreateSwapchain(Swapchain& swapchain,
                                                     VkExtent2D extent) {
    if (!waitIdle()) {
        return SwapchainRecreateResult::fatal;
    }
    switch (swapchain.recreate(extent)) {
        case Swapchain::ReplaceResult::complete:
            return rebuild(swapchain) ? SwapchainRecreateResult::complete
                                      : SwapchainRecreateResult::fatal;
        case Swapchain::ReplaceResult::unavailable:
            return SwapchainRecreateResult::unavailable;
        case Swapchain::ReplaceResult::fatal:
            return SwapchainRecreateResult::fatal;
    }
    return SwapchainRecreateResult::fatal;
}

bool FrameLoop::waitForPresentations() noexcept {
    if (m_presentFences.size() != m_presentPending.size()) {
        std::fprintf(stderr, "Vulkan presentation fence state is invalid\n");
        return false;
    }
    for (std::size_t index = 0; index < m_presentFences.size(); ++index) {
        if (m_presentPending[index] == 0U) {
            continue;
        }
        const VkFence fence = m_presentFences[index];
        if (vkWaitForFences(m_device, 1, &fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS &&
            !m_deviceLost) {
            std::fprintf(stderr, "Vulkan presentation fence wait failed\n");
            return false;
        }
        m_presentPending[index] = 0U;
    }
    return true;
}

bool FrameLoop::waitIdle() noexcept {
    if (vkDeviceWaitIdle(m_device) != VK_SUCCESS && !m_deviceLost) {
        std::fprintf(stderr, "Vulkan device idle wait failed\n");
        return false;
    }
    return waitForPresentations();
}

void FrameLoop::simulateDeviceLoss(DeviceLossSite site) noexcept {
    m_simulatedLoss = site;
}

bool FrameLoop::deviceLost() const noexcept {
    return m_deviceLost;
}

void FrameLoop::reportDeviceLoss(const char* site) noexcept {
    if (!m_deviceLost) {
        std::fprintf(stderr, "Vulkan device lost during %s, tearing down\n", site);
        m_deviceLost = true;
    }
}

void FrameLoop::consumeAcquiredImage(VkSemaphore imageAvailable) noexcept {
    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    static_cast<void>(vkQueueSubmit2(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
}

bool FrameLoop::recordCommands(VkCommandBuffer commandBuffer, const Swapchain& swapchain,
                               std::uint32_t imageIndex) const {
    if (static_cast<std::size_t>(imageIndex) >= swapchain.images().size() ||
        static_cast<std::size_t>(imageIndex) >= swapchain.imageViews().size()) {
        std::fprintf(stderr, "Vulkan acquired an invalid swapchain image\n");
        return false;
    }
    if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan command buffer reset failed\n");
        return false;
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan command buffer begin failed\n");
        return false;
    }

    VkImageMemoryBarrier2 toColor{};
    toColor.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    // The first scope must cover the acquire semaphore's wait stage so the transition
    // chains after the presentation engine releases the image.
    toColor.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.srcAccessMask = VK_ACCESS_2_NONE;
    toColor.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toColor.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toColor.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toColor.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toColor.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toColor.image = swapchain.images()[imageIndex];
    toColor.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toColor.subresourceRange.levelCount = 1;
    toColor.subresourceRange.layerCount = 1;
    VkDependencyInfo toColorDependency{};
    toColorDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toColorDependency.imageMemoryBarrierCount = 1;
    toColorDependency.pImageMemoryBarriers = &toColor;
    vkCmdPipelineBarrier2(commandBuffer, &toColorDependency);

    VkClearValue clearValue{};
    clearValue.color = {{0.035F, 0.045F, 0.08F, 1.0F}};
    VkRenderingAttachmentInfo colorAttachment{};
    colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    colorAttachment.imageView = swapchain.imageViews()[imageIndex];
    colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.clearValue = clearValue;
    const VkExtent2D extent = swapchain.extent();
    VkRenderingInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    renderingInfo.renderArea.extent = extent;
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &colorAttachment;
    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    const VkViewport viewport{
        0.0F, 0.0F, static_cast<float>(extent.width), static_cast<float>(extent.height),
        0.0F, 1.0F};
    const VkRect2D scissor{{0, 0}, extent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
    vkCmdEndRendering(commandBuffer);

    VkImageMemoryBarrier2 toPresent{};
    toPresent.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    toPresent.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    toPresent.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstStageMask = VK_PIPELINE_STAGE_2_NONE;
    toPresent.dstAccessMask = VK_ACCESS_2_NONE;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = swapchain.images()[imageIndex];
    toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toPresent.subresourceRange.levelCount = 1;
    toPresent.subresourceRange.layerCount = 1;
    VkDependencyInfo toPresentDependency{};
    toPresentDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    toPresentDependency.imageMemoryBarrierCount = 1;
    toPresentDependency.pImageMemoryBarriers = &toPresent;
    vkCmdPipelineBarrier2(commandBuffer, &toPresentDependency);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan command buffer end failed\n");
        return false;
    }
    return true;
}

FrameResult FrameLoop::draw(const Swapchain& swapchain) {
    if (m_deviceLost) {
        return FrameResult::deviceLost;
    }
    if (m_fatal) {
        return FrameResult::fatal;
    }
    const FrameResult result = drawFrame(swapchain);
    if (result == FrameResult::fatal) {
        m_fatal = true;
    }
    return result;
}

FrameResult FrameLoop::drawFrame(const Swapchain& swapchain) {
    FrameSlot& frame = m_frames[m_currentFrame];
    if (vkWaitForFences(m_device, 1, &frame.fence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan frame fence wait failed\n");
        return FrameResult::fatal;
    }

    std::uint32_t imageIndex = 0;
    constexpr std::uint64_t acquireTimeoutNanoseconds = 4'000'000U;
    const VkResult acquireResult =
        vkAcquireNextImageKHR(m_device, swapchain.handle(), acquireTimeoutNanoseconds,
                              frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_TIMEOUT || acquireResult == VK_NOT_READY) {
        return FrameResult::idle;
    }
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        return FrameResult::needsRecreate;
    }
    if (acquireResult == VK_ERROR_DEVICE_LOST) {
        reportDeviceLoss("image acquisition");
        return FrameResult::deviceLost;
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        std::fprintf(stderr, "Vulkan image acquisition failed with result %d\n",
                     static_cast<int>(acquireResult));
        return FrameResult::fatal;
    }

    // A successful acquisition leaves a pending signal on the semaphore, so a frame that
    // bails before submitting must still wait it once before the semaphore can die.
    struct AcquireGuard {
        FrameLoop* loop;
        VkSemaphore semaphore;
        bool consumed{false};

        ~AcquireGuard() {
            if (!consumed) {
                loop->consumeAcquiredImage(semaphore);
            }
        }
    };
    AcquireGuard acquireGuard{this, frame.imageAvailable};

    if (static_cast<std::size_t>(imageIndex) >= m_imageFences.size() ||
        static_cast<std::size_t>(imageIndex) >= m_renderFinished.size() ||
        static_cast<std::size_t>(imageIndex) >= m_presentFences.size() ||
        static_cast<std::size_t>(imageIndex) >= m_presentPending.size()) {
        std::fprintf(stderr, "Vulkan image synchronization state is invalid\n");
        return FrameResult::fatal;
    }

    const VkFence imageFence = m_imageFences[imageIndex];
    if (imageFence != VK_NULL_HANDLE &&
        vkWaitForFences(m_device, 1, &imageFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan swapchain image fence wait failed\n");
        return FrameResult::fatal;
    }
    const VkFence presentFence = m_presentFences[imageIndex];
    if (m_presentPending[imageIndex] != 0U &&
        vkWaitForFences(m_device, 1, &presentFence, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan presentation fence wait failed\n");
        return FrameResult::fatal;
    }
    m_presentPending[imageIndex] = 0U;
    if (vkResetFences(m_device, 1, &presentFence) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan presentation fence reset failed\n");
        return FrameResult::fatal;
    }
    if (!recordCommands(frame.commandBuffer, swapchain, imageIndex)) {
        return FrameResult::fatal;
    }
    if (vkResetFences(m_device, 1, &frame.fence) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan frame fence reset failed\n");
        return FrameResult::fatal;
    }

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = frame.commandBuffer;
    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = m_renderFinished[imageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    VkSubmitInfo2 submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submitInfo.waitSemaphoreInfoCount = 1;
    submitInfo.pWaitSemaphoreInfos = &waitInfo;
    submitInfo.commandBufferInfoCount = 1;
    submitInfo.pCommandBufferInfos = &commandInfo;
    submitInfo.signalSemaphoreInfoCount = 1;
    submitInfo.pSignalSemaphoreInfos = &signalInfo;
    VkResult submitResult = vkQueueSubmit2(m_graphicsQueue, 1, &submitInfo, frame.fence);
    if (submitResult == VK_SUCCESS) {
        m_imageFences[imageIndex] = frame.fence;
        acquireGuard.consumed = true;
    }
    if (m_simulatedLoss == DeviceLossSite::submit) {
        m_simulatedLoss = DeviceLossSite::none;
        submitResult = VK_ERROR_DEVICE_LOST;
    }
    if (submitResult == VK_ERROR_DEVICE_LOST) {
        reportDeviceLoss("queue submission");
        return FrameResult::deviceLost;
    }
    if (submitResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan queue submission failed with result %d\n",
                     static_cast<int>(submitResult));
        return FrameResult::fatal;
    }

    const VkSwapchainKHR swapchainHandle = swapchain.handle();
    VkSwapchainPresentFenceInfoEXT presentFenceInfo{};
    presentFenceInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT;
    presentFenceInfo.swapchainCount = 1;
    presentFenceInfo.pFences = &presentFence;
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.pNext = &presentFenceInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinished[imageIndex];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchainHandle;
    presentInfo.pImageIndices = &imageIndex;
    VkResult presentResult = vkQueuePresentKHR(m_presentQueue, &presentInfo);
    if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR ||
        presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_ERROR_SURFACE_LOST_KHR) {
        m_presentPending[imageIndex] = 1U;
    }

    m_currentFrame = (m_currentFrame + 1U) % m_frames.size();
    if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR) {
        ++m_frameCount;
    }
    if (m_simulatedLoss == DeviceLossSite::present) {
        m_simulatedLoss = DeviceLossSite::none;
        presentResult = VK_ERROR_DEVICE_LOST;
    }
    if (presentResult == VK_ERROR_DEVICE_LOST) {
        reportDeviceLoss("presentation");
        return FrameResult::deviceLost;
    }
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR ||
        acquireResult == VK_SUBOPTIMAL_KHR) {
        return FrameResult::needsRecreate;
    }
    if (presentResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan presentation failed with result %d\n",
                     static_cast<int>(presentResult));
        return FrameResult::fatal;
    }
    return FrameResult::rendered;
}

void FrameLoop::destroy() noexcept {
    destroySemaphores(m_device, m_renderFinished);
    m_renderFinished.clear();
    destroyFences(m_device, m_presentFences);
    m_presentFences.clear();
    m_presentPending.clear();
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
        m_pipeline = VK_NULL_HANDLE;
    }
    if (m_pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        m_pipelineLayout = VK_NULL_HANDLE;
    }
    for (FrameSlot& frame : m_frames) {
        if (frame.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
            frame.imageAvailable = VK_NULL_HANDLE;
        }
        if (frame.fence != VK_NULL_HANDLE) {
            vkDestroyFence(m_device, frame.fence, nullptr);
            frame.fence = VK_NULL_HANDLE;
        }
    }
    if (m_commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(m_device, m_commandPool, nullptr);
        m_commandPool = VK_NULL_HANDLE;
    }
}

std::uint64_t FrameLoop::frameCount() const noexcept {
    return m_frameCount;
}

}

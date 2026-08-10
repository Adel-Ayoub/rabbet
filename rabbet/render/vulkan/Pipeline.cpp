#include "rabbet/render/vulkan/Pipeline.h"

#include "rabbet/render/vulkan/Descriptors.h"
#include "rabbet/render/vulkan/Device.h"

#include <array>
#include <cstdio>
#include <vector>

namespace rb::vulkan {

namespace {

VkShaderModule createShaderModule(VkDevice device, std::span<const std::uint32_t> code) {
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

VkPipelineColorBlendAttachmentState blendAttachment(BlendMode blendMode) noexcept {
    VkPipelineColorBlendAttachmentState state{};
    state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                           VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    switch (blendMode) {
        case BlendMode::opaque:
            break;
        case BlendMode::alphaBlend:
            state.blendEnable = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.colorBlendOp = VK_BLEND_OP_ADD;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            state.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
        case BlendMode::additive:
            state.blendEnable = VK_TRUE;
            state.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            state.colorBlendOp = VK_BLEND_OP_ADD;
            state.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            state.alphaBlendOp = VK_BLEND_OP_ADD;
            break;
    }
    return state;
}

bool validDescription(const PipelineDescription& description, const DeviceLimits& limits) {
    if (description.vertexCode.empty() || description.fragmentCode.empty()) {
        std::fprintf(stderr, "Vulkan pipeline description is missing shader code\n");
        return false;
    }
    if (description.colorFormats.empty() && description.depthFormat == VK_FORMAT_UNDEFINED) {
        std::fprintf(stderr, "Vulkan pipeline description names no attachment format\n");
        return false;
    }
    if ((description.depthTest || description.depthWrite) &&
        description.depthFormat == VK_FORMAT_UNDEFINED) {
        std::fprintf(stderr, "Vulkan pipeline description uses depth without a depth format\n");
        return false;
    }
    if (description.vertexStride == 0U && !description.vertexAttributes.empty()) {
        std::fprintf(stderr, "Vulkan pipeline description has attributes without a stride\n");
        return false;
    }
    if (description.setLayouts.size() > maxDescriptorSetCount) {
        std::fprintf(stderr, "Vulkan pipeline description exceeds the descriptor set budget\n");
        return false;
    }
    if (description.pushConstantBytes > maxPushConstantBytes ||
        description.pushConstantBytes > limits.pushConstantBytes ||
        description.pushConstantBytes % 4U != 0U) {
        std::fprintf(stderr, "Vulkan pipeline description exceeds the push constant budget\n");
        return false;
    }
    if (description.pushConstantBytes > 0U && description.pushConstantStages == 0U) {
        std::fprintf(stderr, "Vulkan pipeline description pushes constants to no stage\n");
        return false;
    }
    return true;
}

}

std::unique_ptr<Pipeline> Pipeline::create(const Device& device,
                                           const PipelineDescription& description,
                                           VkPipelineCache cache) {
    if (!validDescription(description, device.limits())) {
        return nullptr;
    }

    const VkShaderModule vertexModule = createShaderModule(device.handle(),
                                                           description.vertexCode);
    const VkShaderModule fragmentModule = createShaderModule(device.handle(),
                                                             description.fragmentCode);
    if (vertexModule == VK_NULL_HANDLE || fragmentModule == VK_NULL_HANDLE) {
        std::fprintf(stderr, "Vulkan shader module creation failed\n");
        if (vertexModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device.handle(), vertexModule, nullptr);
        }
        if (fragmentModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device.handle(), fragmentModule, nullptr);
        }
        return nullptr;
    }

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = static_cast<std::uint32_t>(description.setLayouts.size());
    layoutInfo.pSetLayouts = description.setLayouts.data();
    VkPushConstantRange pushRange{};
    if (description.pushConstantBytes > 0U) {
        pushRange.stageFlags = description.pushConstantStages;
        pushRange.size = description.pushConstantBytes;
        layoutInfo.pushConstantRangeCount = 1;
        layoutInfo.pPushConstantRanges = &pushRange;
    }
    VkPipelineLayout layout = VK_NULL_HANDLE;
    if (vkCreatePipelineLayout(device.handle(), &layoutInfo, nullptr, &layout) != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan pipeline layout creation failed\n");
        vkDestroyShaderModule(device.handle(), fragmentModule, nullptr);
        vkDestroyShaderModule(device.handle(), vertexModule, nullptr);
        return nullptr;
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

    VkVertexInputBindingDescription binding{};
    std::vector<VkVertexInputAttributeDescription> attributes;
    VkPipelineVertexInputStateCreateInfo vertexInput{};
    vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (description.vertexStride > 0U) {
        binding.binding = 0;
        binding.stride = description.vertexStride;
        binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        attributes.reserve(description.vertexAttributes.size());
        for (const VertexAttribute& attribute : description.vertexAttributes) {
            VkVertexInputAttributeDescription vkAttribute{};
            vkAttribute.location = attribute.location;
            vkAttribute.binding = 0;
            vkAttribute.format = attribute.format;
            vkAttribute.offset = attribute.offset;
            attributes.push_back(vkAttribute);
        }
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &binding;
        vertexInput.vertexAttributeDescriptionCount =
            static_cast<std::uint32_t>(attributes.size());
        vertexInput.pVertexAttributeDescriptions = attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = description.topology;
    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo rasterization{};
    rasterization.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterization.polygonMode = VK_POLYGON_MODE_FILL;
    rasterization.cullMode = description.cullMode;
    rasterization.frontFace = description.frontFace;
    rasterization.lineWidth = 1.0F;
    VkPipelineMultisampleStateCreateInfo multisample{};
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = description.depthTest ? VK_TRUE : VK_FALSE;
    depthStencil.depthWriteEnable = description.depthWrite ? VK_TRUE : VK_FALSE;
    depthStencil.depthCompareOp = description.depthCompare;

    const VkPipelineColorBlendAttachmentState attachmentBlend =
        blendAttachment(description.blendMode);
    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(
        description.colorFormats.size(), attachmentBlend);
    VkPipelineColorBlendStateCreateInfo colorBlend{};
    colorBlend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlend.attachmentCount = static_cast<std::uint32_t>(blendAttachments.size());
    colorBlend.pAttachments = blendAttachments.data();

    constexpr std::array dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount =
        static_cast<std::uint32_t>(description.colorFormats.size());
    renderingInfo.pColorAttachmentFormats = description.colorFormats.data();
    renderingInfo.depthAttachmentFormat = description.depthFormat;

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
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlend;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = layout;

    VkPipeline pipeline = VK_NULL_HANDLE;
    const VkResult pipelineResult = vkCreateGraphicsPipelines(device.handle(), cache, 1,
                                                              &pipelineInfo, nullptr, &pipeline);
    vkDestroyShaderModule(device.handle(), fragmentModule, nullptr);
    vkDestroyShaderModule(device.handle(), vertexModule, nullptr);
    if (pipelineResult != VK_SUCCESS) {
        std::fprintf(stderr, "Vulkan graphics pipeline creation failed with result %d\n",
                     static_cast<int>(pipelineResult));
        vkDestroyPipelineLayout(device.handle(), layout, nullptr);
        return nullptr;
    }
    return std::unique_ptr<Pipeline>(new Pipeline(device.handle(), layout, pipeline));
}

Pipeline::Pipeline(VkDevice device, VkPipelineLayout layout, VkPipeline pipeline) noexcept
    : m_device(device), m_layout(layout), m_pipeline(pipeline) {}

Pipeline::~Pipeline() {
    if (m_pipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(m_device, m_pipeline, nullptr);
    }
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(m_device, m_layout, nullptr);
    }
}

VkPipeline Pipeline::handle() const noexcept {
    return m_pipeline;
}

VkPipelineLayout Pipeline::layout() const noexcept {
    return m_layout;
}

}

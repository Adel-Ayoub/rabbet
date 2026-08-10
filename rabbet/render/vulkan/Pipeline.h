#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <memory>
#include <span>

namespace rb::vulkan {

class Device;

enum class BlendMode {
    opaque,
    alphaBlend,
    additive,
};

struct VertexAttribute {
    std::uint32_t location{0};
    VkFormat format{VK_FORMAT_UNDEFINED};
    std::uint32_t offset{0};
};

struct PipelineDescription {
    std::span<const std::uint32_t> vertexCode;
    std::span<const std::uint32_t> fragmentCode;
    std::uint32_t vertexStride{0};
    std::span<const VertexAttribute> vertexAttributes;
    VkPrimitiveTopology topology{VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST};
    VkCullModeFlags cullMode{VK_CULL_MODE_NONE};
    VkFrontFace frontFace{VK_FRONT_FACE_COUNTER_CLOCKWISE};
    BlendMode blendMode{BlendMode::opaque};
    bool depthTest{false};
    bool depthWrite{false};
    VkCompareOp depthCompare{VK_COMPARE_OP_LESS};
    std::span<const VkFormat> colorFormats;
    VkFormat depthFormat{VK_FORMAT_UNDEFINED};
    std::span<const VkDescriptorSetLayout> setLayouts;
    std::uint32_t pushConstantBytes{0};
    VkShaderStageFlags pushConstantStages{VK_SHADER_STAGE_VERTEX_BIT |
                                          VK_SHADER_STAGE_FRAGMENT_BIT};
};

class Pipeline {
public:
    static std::unique_ptr<Pipeline> create(const Device& device,
                                            const PipelineDescription& description,
                                            VkPipelineCache cache);

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;
    ~Pipeline();

    [[nodiscard]] VkPipeline handle() const noexcept;
    [[nodiscard]] VkPipelineLayout layout() const noexcept;

private:
    Pipeline(VkDevice device, VkPipelineLayout layout, VkPipeline pipeline) noexcept;

    VkDevice m_device{VK_NULL_HANDLE};
    VkPipelineLayout m_layout{VK_NULL_HANDLE};
    VkPipeline m_pipeline{VK_NULL_HANDLE};
};

}

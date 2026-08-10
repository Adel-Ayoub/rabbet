#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec4 color;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform FrameData {
    vec4 tint;
} frameData;

layout(set = 1, binding = 0) uniform sampler2D albedo;

layout(push_constant) uniform DrawData {
    vec4 tint;
} drawData;

void main() {
    outColor = texture(albedo, uv) * color * frameData.tint * drawData.tint;
}

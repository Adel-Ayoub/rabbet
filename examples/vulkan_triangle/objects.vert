#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUv;
layout(location = 2) in vec4 inColor;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec4 color;

void main() {
    gl_Position = vec4(inPosition, 1.0);
    uv = inUv;
    color = inColor;
}

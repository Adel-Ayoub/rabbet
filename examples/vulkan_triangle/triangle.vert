#version 450

layout(location = 0) out vec3 color;

void main() {
    const vec2 positions[3] = vec2[3](
        vec2(0.0, -0.65),
        vec2(0.65, 0.55),
        vec2(-0.65, 0.55));
    const vec3 colors[3] = vec3[3](
        vec3(1.0, 0.32, 0.18),
        vec3(0.22, 0.84, 0.78),
        vec3(0.98, 0.78, 0.24));

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    color = colors[gl_VertexIndex];
}

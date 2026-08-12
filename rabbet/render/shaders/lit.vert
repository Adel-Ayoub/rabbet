layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
RB_PER_FRAME_BEGIN
    mat4 uViewProjection;
RB_PER_FRAME_END
RB_PER_DRAW_BEGIN
RB_PER_DRAW_AT(mat4 uModel, 0)
RB_PER_DRAW_AT(mat3 uNormalMatrix, 64)
RB_PER_DRAW_END
layout(location = 0) out vec3 vNormal;
layout(location = 1) out vec3 vWorldPos;
layout(location = 2) out vec2 vUv;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUv = aUv;
    gl_Position = uViewProjection * world;
}

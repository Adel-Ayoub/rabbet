layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform mat4 uViewProjection;
uniform float uHeightScale;
out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUv;
out float vHeight01;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUv = aUv;
    vHeight01 = clamp(aPosition.y / max(uHeightScale, 0.0001), 0.0, 1.0);
    gl_Position = uViewProjection * world;
}

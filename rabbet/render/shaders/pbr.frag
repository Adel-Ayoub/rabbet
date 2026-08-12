layout(location = 0) in vec3 vNormal;
layout(location = 1) in vec3 vWorldPos;
layout(location = 2) in vec2 vUv;
layout(location = 0) out vec4 FragColor;
RB_MATERIAL_BEGIN
RB_MATERIAL_AT(vec3 uBaseColor, 0)
RB_MATERIAL_AT(float uMetallic, 12)
RB_MATERIAL_AT(vec3 uEmissive, 16)
RB_MATERIAL_AT(float uRoughness, 28)
RB_MATERIAL_AT(float uAo, 32)
RB_MATERIAL_END
RB_MATERIAL_SAMPLER(1, sampler2D uAlbedoTex)
const float PI = 3.14159265359;
#include "light_uniforms.glsl"
#include "shadow_functions.glsl"
#include "pbr_functions.glsl"

void main() {
    vec3 albedo = texture(uAlbedoTex, vUv).rgb * uBaseColor;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPosition - vWorldPos);
    vec3 f0 = mix(vec3(0.04), albedo, uMetallic);
    vec3 lo = vec3(0.0);
    for (int i = 0; i < uDirectionalCount; ++i) {
        vec3 L = normalize(-uDirectionalDirection[i]);
        vec3 contribution = brdf(L, uDirectionalColor[i], N, V, albedo, f0);
        if (i == 0) contribution *= (1.0 - shadowFactor(vWorldPos, N, L));
        lo += contribution;
    }
    for (int i = 0; i < uPointCount; ++i) {
        vec3 toLight = uPointPosition[i] - vWorldPos;
        float distance = length(toLight);
        vec3 a = uPointAttenuation[i];
        float attenuation = 1.0 / (a.x + a.y * distance + a.z * distance * distance);
        lo += brdf(toLight / max(distance, 0.0001), uPointColor[i] * attenuation, N, V, albedo, f0);
    }
    for (int i = 0; i < uSpotCount; ++i) {
        vec3 L;
        float attenuation;
        float cone = spotFalloff(i, uSpotPosition[i] - vWorldPos, L, attenuation);
        lo += brdf(L, uSpotColor[i] * attenuation * cone, N, V, albedo, f0);
    }
    vec3 ambient = ambientLight(N) * albedo * uAo;
    vec3 color = ambient + lo + uEmissive;
    // With post-processing on, the scene renders into a linear HDR target and tone-mapping + gamma
    // happen in the composite pass; otherwise tone-map and encode here exactly as before.
    if (uHdrOutput == 0) {
        color = color / (color + vec3(1.0));
        color = pow(color, vec3(1.0 / 2.2));
    }
    FragColor = vec4(color, 1.0);
}

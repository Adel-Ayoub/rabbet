in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec3 uTint;
uniform vec3 uEmissive;
uniform float uSpecularStrength;
uniform float uShininess;
#include "light_uniforms.glsl"
#include "shadow_functions.glsl"

vec3 shade(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo) {
    float diffuse = max(dot(N, L), 0.0);
    vec3 reflection = reflect(-L, N);
    float specular = pow(max(dot(V, reflection), 0.0), uShininess) * uSpecularStrength;
    return radiance * (diffuse * albedo + vec3(specular));
}
void main() {
    vec3 albedo = texture(uTexture, vUv).rgb * uTint;
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPosition - vWorldPos);
    vec3 color = ambientLight(N) * albedo;
    for (int i = 0; i < uDirectionalCount; ++i) {
        vec3 L = normalize(-uDirectionalDirection[i]);
        vec3 contribution = shade(L, uDirectionalColor[i], N, V, albedo);
        if (i == 0) contribution *= (1.0 - shadowFactor(vWorldPos, N, L));
        color += contribution;
    }
    for (int i = 0; i < uPointCount; ++i) {
        vec3 toLight = uPointPosition[i] - vWorldPos;
        float distance = length(toLight);
        vec3 a = uPointAttenuation[i];
        float attenuation = 1.0 / (a.x + a.y * distance + a.z * distance * distance);
        color += shade(toLight / max(distance, 0.0001), uPointColor[i] * attenuation, N, V, albedo);
    }
    for (int i = 0; i < uSpotCount; ++i) {
        vec3 L;
        float attenuation;
        float cone = spotFalloff(i, uSpotPosition[i] - vWorldPos, L, attenuation);
        color += shade(L, uSpotColor[i] * attenuation * cone, N, V, albedo);
    }
    color += uEmissive;
    FragColor = vec4(color, 1.0);
}

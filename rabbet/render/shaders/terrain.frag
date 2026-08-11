in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUv;
in float vHeight01;
out vec4 FragColor;
const int kMaxTerrainLayers = 4;
uniform int uLayerCount;
uniform sampler2D uLayerAlbedo[kMaxTerrainLayers];
uniform float uLayerTiling[kMaxTerrainLayers];
uniform vec2 uLayerHeightRange[kMaxTerrainLayers];
uniform vec2 uLayerSlopeRange[kMaxTerrainLayers];
uniform float uLayerSharpness[kMaxTerrainLayers];
uniform int uBlendMode; // 0 = height/slope auto-blend, 1 = splat map
uniform sampler2D uSplat;
uniform int uHasSplat;
uniform float uMetallic;
uniform float uRoughness;
uniform int uHdrOutput; // 1: emit linear HDR for the post pipeline; 0 (default): inline tonemap+gamma
const float PI = 3.14159265359;
#include "light_uniforms.glsl"
#include "shadow_functions.glsl"
#include "pbr_functions.glsl"

// Soft plateau in [range.x, range.y] with smoothstep edges of half-width `sharp`.
float terrainBand(float v, vec2 range, float sharp) {
    float lo = smoothstep(range.x - sharp, range.x + sharp, v);
    float hi = 1.0 - smoothstep(range.y - sharp, range.y + sharp, v);
    return clamp(lo * hi, 0.0, 1.0);
}
// A cheap value-noise tint to break the obvious repeat of a tiled albedo (cosmetic only).
float terrainHash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float terrainNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    float a = terrainHash(i);
    float b = terrainHash(i + vec2(1.0, 0.0));
    float c = terrainHash(i + vec2(0.0, 1.0));
    float d = terrainHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
// One layer's weighted albedo (rgb) and weight (a). Constant sampler indices only, so it is
// portable to drivers that reject dynamically-indexed sampler arrays (e.g. macOS GL 4.1).
vec4 layerContribution(sampler2D tex, float tiling, vec2 hRange, vec2 sRange, float sharp,
                       float splatWeight, bool useSplat, float height01, float slope) {
    float w = useSplat ? splatWeight
                       : terrainBand(height01, hRange, sharp) * terrainBand(slope, sRange, sharp);
    return vec4(texture(tex, vUv * tiling).rgb * w, w);
}
vec3 terrainAlbedo(float height01, float slope) {
    bool useSplat = (uBlendMode == 1 && uHasSplat == 1);
    vec4 splatW = useSplat ? texture(uSplat, vUv) : vec4(0.0);
    vec4 acc = vec4(0.0);
    if (uLayerCount > 0) acc += layerContribution(uLayerAlbedo[0], uLayerTiling[0], uLayerHeightRange[0], uLayerSlopeRange[0], uLayerSharpness[0], splatW.x, useSplat, height01, slope);
    if (uLayerCount > 1) acc += layerContribution(uLayerAlbedo[1], uLayerTiling[1], uLayerHeightRange[1], uLayerSlopeRange[1], uLayerSharpness[1], splatW.y, useSplat, height01, slope);
    if (uLayerCount > 2) acc += layerContribution(uLayerAlbedo[2], uLayerTiling[2], uLayerHeightRange[2], uLayerSlopeRange[2], uLayerSharpness[2], splatW.z, useSplat, height01, slope);
    if (uLayerCount > 3) acc += layerContribution(uLayerAlbedo[3], uLayerTiling[3], uLayerHeightRange[3], uLayerSlopeRange[3], uLayerSharpness[3], splatW.w, useSplat, height01, slope);
    if (acc.a <= 0.0001) {
        return texture(uLayerAlbedo[0], vUv * uLayerTiling[0]).rgb; // no band matched -> base layer
    }
    vec3 albedo = acc.rgb / acc.a;
    return albedo * mix(0.92, 1.06, terrainNoise(vWorldPos.xz * 0.05));
}
void main() {
    vec3 N = normalize(vNormal);
    float slope = 1.0 - clamp(N.y, 0.0, 1.0);
    vec3 albedo = terrainAlbedo(vHeight01, slope);
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
    vec3 ambient = ambientLight(N) * albedo;
    vec3 color = ambient + lo;
    if (uHdrOutput == 0) {
        color = color / (color + vec3(1.0));
        color = pow(color, vec3(1.0 / 2.2));
    }
    FragColor = vec4(color, 1.0);
}

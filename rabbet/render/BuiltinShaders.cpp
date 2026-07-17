#include "rabbet/render/BuiltinShaders.h"

#include <utility>

#include "rabbet/assets/AssetManager.h"
#include "rabbet/render/MaterialAsset.h"
#include "rabbet/render/ShaderAsset.h"

namespace rb {
namespace {

constexpr const char* kVertexSource = R"(#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform mat4 uViewProjection;
out vec3 vNormal;
out vec3 vWorldPos;
out vec2 vUv;
void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vNormal = uNormalMatrix * aNormal;
    vUv = aUv;
    gl_Position = uViewProjection * world;
}
)";

constexpr const char* kShadowFunctions = R"(
uniform mat4 uLightSpace;
uniform sampler2D uShadowMap;
uniform int uHasShadowMap;
float shadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    if (uHasShadowMap == 0) return 0.0;
    vec4 lightClip = uLightSpace * vec4(worldPos, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    proj = proj * 0.5 + 0.5;
    if (proj.z > 1.0) return 0.0;
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0005);
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float depth = texture(uShadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += proj.z - bias > depth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}
)";

constexpr const char* kLightUniforms = R"(
const int kMaxDir = 4;
const int kMaxPoint = 8;
const int kMaxSpot = 4;
uniform vec3 uViewPosition;
uniform vec3 uAmbient;
uniform int uDirectionalCount;
uniform vec3 uDirectionalDirection[kMaxDir];
uniform vec3 uDirectionalColor[kMaxDir];
uniform int uPointCount;
uniform vec3 uPointPosition[kMaxPoint];
uniform vec3 uPointColor[kMaxPoint];
uniform vec3 uPointAttenuation[kMaxPoint];
uniform int uSpotCount;
uniform vec3 uSpotPosition[kMaxSpot];
uniform vec3 uSpotDirection[kMaxSpot];
uniform vec3 uSpotColor[kMaxSpot];
uniform vec3 uSpotAttenuation[kMaxSpot];
uniform vec2 uSpotCone[kMaxSpot]; // x = cos(inner), y = cos(outer)
uniform samplerCube uIrradiance;
uniform int uHasEnvironment;
uniform float uEnvironmentIntensity;
// Ambient light reaching a surface with normal N: convolved environment irradiance when enabled,
// otherwise the flat ambient constant (so the result is identical to the pre-IBL path).
vec3 ambientLight(vec3 N) {
    if (uHasEnvironment == 1) {
        return texture(uIrradiance, N).rgb * uEnvironmentIntensity;
    }
    return uAmbient;
}
float spotFalloff(int i, vec3 toLight, out vec3 L, out float attenuation) {
    float dist = length(toLight);
    L = toLight / max(dist, 0.0001);
    vec3 a = uSpotAttenuation[i];
    attenuation = 1.0 / (a.x + a.y * dist + a.z * dist * dist);
    float cosTheta = dot(L, normalize(-uSpotDirection[i]));
    return clamp((cosTheta - uSpotCone[i].y) / max(uSpotCone[i].x - uSpotCone[i].y, 0.0001),
                 0.0, 1.0);
}
)";

// The Cook-Torrance BRDF helpers shared by the PBR and terrain fragment shaders, so the two cannot
// drift. brdf() reads the global uRoughness/uMetallic uniforms (every user declares them) and PI;
// the rest are pure. Equivalent to the original inline PBR helpers, plus an a2 floor so a zero
// roughness cannot produce 0/0 = NaN in the NDF when H aligns exactly with N.
constexpr const char* kPbrFunctions = R"(
float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = max(a * a, 1.0e-6);
    float nh = max(dot(N, H), 0.0);
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float geometrySchlick(float nv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nv / (nv * (1.0 - k) + k);
}
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometrySchlick(max(dot(N, V), 0.0), roughness) *
           geometrySchlick(max(dot(N, L), 0.0), roughness);
}
vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 brdf(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo, vec3 f0) {
    vec3 H = normalize(V + L);
    float ndf = distributionGGX(N, H, uRoughness);
    float g = geometrySmith(N, V, L, uRoughness);
    vec3 f = fresnelSchlick(max(dot(H, V), 0.0), f0);
    vec3 specular = (ndf * g * f) /
                    (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3 kd = (vec3(1.0) - f) * (1.0 - uMetallic);
    return (kd * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
}
)";

} // namespace

const std::string& builtinLitVertexSource() {
    static const std::string source = kVertexSource;
    return source;
}

const std::string& builtinPhongFragmentSource() {
    static const std::string source = std::string(R"(#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uTexture;
uniform vec3 uTint;
uniform vec3 uEmissive;
uniform float uSpecularStrength;
uniform float uShininess;
)") + kLightUniforms + kShadowFunctions + R"(
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
)";
    return source;
}

const std::string& builtinPbrFragmentSource() {
    static const std::string source = std::string(R"(#version 410 core
in vec3 vNormal;
in vec3 vWorldPos;
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uAlbedoTex;
uniform vec3 uBaseColor;
uniform vec3 uEmissive;
uniform float uMetallic;
uniform float uRoughness;
uniform float uAo;
uniform int uHdrOutput; // 1: emit linear HDR for the post pipeline; 0 (default): inline tonemap+gamma
const float PI = 3.14159265359;
)") + kLightUniforms + kShadowFunctions + kPbrFunctions + R"(
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
)";
    return source;
}

const std::string& builtinTerrainVertexSource() {
    // Like the lit vertex shader, but also forwards the normalized local height (y in [0,heightScale]
    // -> [0,1]) so the fragment can blend layers by elevation independent of the entity's transform.
    static const std::string source = R"(#version 410 core
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
)";
    return source;
}

const std::string& builtinTerrainFragmentSource() {
    // A lit, splat-blended terrain shader. It samples up to four albedo layers, weights them either
    // by each layer's height + slope band (auto-blend) or by an RGBA splat map, then runs the SAME
    // directional/point/spot light loop + shadows + ambient/IBL as the PBR shader (shared brdf and
    // light helpers) and honours the uHdrOutput seam so post-processing tone-maps it.
    static const std::string source = std::string(R"(#version 410 core
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
)") + kLightUniforms + kShadowFunctions + kPbrFunctions + R"(
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
)";
    return source;
}

void registerDefaultRenderAssets(AssetManager& assets) {
    if (assets.find<ShaderAsset>(builtin::kPbrShader).valid()) {
        return; // already registered
    }

    ShaderAsset pbr;
    pbr.vertexSource = builtinLitVertexSource();
    pbr.fragmentSource = builtinPbrFragmentSource();
    assets.add<ShaderAsset>(std::move(pbr), builtin::kPbrShader);

    ShaderAsset phong;
    phong.vertexSource = builtinLitVertexSource();
    phong.fragmentSource = builtinPhongFragmentSource();
    assets.add<ShaderAsset>(std::move(phong), builtin::kPhongShader);

    MaterialAsset material;
    material.shader = builtin::kPbrShader;
    assets.add<MaterialAsset>(std::move(material), builtin::kDefaultMaterial);
}

} // namespace rb

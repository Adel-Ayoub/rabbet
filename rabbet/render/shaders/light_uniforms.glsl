#ifndef RB_LIGHT_FRAME_BEGIN
#define RB_LIGHT_FRAME_BEGIN
#define RB_LIGHT_FRAME_END
#define RB_LIGHT_FRAME_AT(declaration, byte_offset) uniform declaration;
#define RB_FRAME_SAMPLER(slot, declaration) uniform declaration;
#endif

const int kMaxDir = 4;
const int kMaxPoint = 8;
const int kMaxSpot = 4;
RB_LIGHT_FRAME_BEGIN
RB_LIGHT_FRAME_AT(vec3 uViewPosition, 0)
RB_LIGHT_FRAME_AT(vec3 uAmbient, 16)
RB_LIGHT_FRAME_AT(int uDirectionalCount, 28)
RB_LIGHT_FRAME_AT(vec3 uDirectionalDirection[kMaxDir], 32)
RB_LIGHT_FRAME_AT(vec3 uDirectionalColor[kMaxDir], 96)
RB_LIGHT_FRAME_AT(int uPointCount, 160)
RB_LIGHT_FRAME_AT(vec3 uPointPosition[kMaxPoint], 176)
RB_LIGHT_FRAME_AT(vec3 uPointColor[kMaxPoint], 304)
RB_LIGHT_FRAME_AT(vec3 uPointAttenuation[kMaxPoint], 432)
RB_LIGHT_FRAME_AT(int uSpotCount, 560)
RB_LIGHT_FRAME_AT(vec3 uSpotPosition[kMaxSpot], 576)
RB_LIGHT_FRAME_AT(vec3 uSpotDirection[kMaxSpot], 640)
RB_LIGHT_FRAME_AT(vec3 uSpotColor[kMaxSpot], 704)
RB_LIGHT_FRAME_AT(vec3 uSpotAttenuation[kMaxSpot], 768)
RB_LIGHT_FRAME_AT(vec2 uSpotCone[kMaxSpot], 832)
RB_LIGHT_FRAME_AT(int uHasEnvironment, 896)
RB_LIGHT_FRAME_AT(float uEnvironmentIntensity, 900)
RB_LIGHT_FRAME_AT(mat4 uLightSpace, 912)
RB_LIGHT_FRAME_AT(int uHasShadowMap, 976)
RB_LIGHT_FRAME_AT(int uHdrOutput, 980)
RB_LIGHT_FRAME_END
RB_FRAME_SAMPLER(2, samplerCube uIrradiance)
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

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

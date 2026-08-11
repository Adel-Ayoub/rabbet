in vec3 vWorldPos;
in vec2 vLocal;
out vec4 FragColor;

uniform vec3 uViewPosition;
uniform int uHdrOutput;
uniform vec3 uAmbient;
uniform int uDirectionalCount;
uniform vec3 uDirectionalDirection[4];
uniform vec3 uDirectionalColor[4];

uniform float uTime; // already carries the surface's wave speed, wrapped on a whole cycle
uniform float uWaveTileScale;
uniform float uWaveStrength;
uniform float uSmoothness;
uniform vec2 uExtent;
uniform vec4 uDeepColor;
uniform vec4 uShallowColor;
uniform samplerCube uSkybox;
uniform int uHasSkybox;

// Exact gradient of the height field sum(A * sin(f * dot(d, p) + w * t)) in tile space. The world
// slope would carry a further factor of uWaveTileScale; leaving it out is what keeps wave SIZE
// (tile scale) and wave STEEPNESS (strength) independent knobs for the author.
vec3 waveNormal(vec2 p, float t) {
    const vec2 d0 = vec2(0.9438, 0.3305);
    const vec2 d1 = vec2(-0.4818, 0.8763);
    const vec2 d2 = vec2(0.1961, -0.9806);
    vec2 grad = vec2(0.0);
    grad += d0 * (1.00 * 1.0 * cos(dot(d0, p) * 1.0 + t * 1.00));
    grad += d1 * (2.30 * 0.45 * cos(dot(d1, p) * 2.3 + t * 1.25));
    grad += d2 * (4.10 * 0.20 * cos(dot(d2, p) * 4.1 + t * 1.75));
    return normalize(vec3(-grad.x * uWaveStrength, 1.0, -grad.y * uWaveStrength));
}

void main() {
    vec3 N = waveNormal(vWorldPos.xz * uWaveTileScale, uTime);
    vec3 V = normalize(uViewPosition - vWorldPos);
    // The pass draws both faces, so shade against whichever one the eye is on; otherwise every
    // fragment of the underside clamps to ndv 0 and reads as one flat mirror.
    N = faceforward(N, -V, N);
    float ndv = clamp(dot(N, V), 0.0, 1.0);
    float fresnel = 0.02 + 0.98 * pow(1.0 - ndv, 5.0);

    vec4 base = mix(uShallowColor, uDeepColor, ndv);
    vec3 refl = base.rgb;
    if (uHasSkybox == 1) {
        vec3 R = reflect(-V, N);
        R.y = max(R.y, 0.02); // mirror only the sky half: the cube has no underside worth showing
        refl = texture(uSkybox, R).rgb;
    }
    vec3 color = mix(base.rgb, refl, fresnel);

    float specExp = mix(16.0, 512.0, clamp(uSmoothness, 0.0, 1.0));
    for (int i = 0; i < uDirectionalCount; ++i) {
        vec3 L = normalize(-uDirectionalDirection[i]);
        vec3 H = normalize(L + V);
        color += uDirectionalColor[i] * pow(max(dot(N, H), 0.0), specExp) * fresnel * 4.0;
    }
    color += uAmbient * base.rgb;

    // Fade over a fixed world width so a long canal feathers by the same amount on both axes.
    const float kFadeWidth = 2.0;
    vec2 toEdge = (1.0 - abs(vLocal)) * uExtent;
    float edge = smoothstep(0.0, min(kFadeWidth, uExtent.x), toEdge.x) *
                 smoothstep(0.0, min(kFadeWidth, uExtent.y), toEdge.y);
    // A mirror is opaque: alpha has to rise with fresnel, or the reflection is washed out by the
    // scene behind it exactly where it is strongest.
    float body = clamp(mix(uShallowColor.a, uDeepColor.a, ndv), 0.0, 1.0);
    float alpha = mix(body, 1.0, fresnel) * edge;

    if (uHdrOutput == 0) {
        color = color / (color + vec3(1.0));
        color = pow(color, vec3(1.0 / 2.2));
    }
    FragColor = vec4(color, alpha);
}

in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform int uBloomEnabled;
uniform float uBloomIntensity;
uniform float uExposure;
uniform int uTonemap;
uniform float uGamma;
uniform float uContrast;
uniform float uSaturation;
uniform float uVignette;

vec3 tonemapReinhard(vec3 c) { return c / (c + vec3(1.0)); }
vec3 tonemapAces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 hablePartial(vec3 x) {
    const float a = 0.15, b = 0.50, c = 0.10, d = 0.20, e = 0.02, f = 0.30;
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}
vec3 tonemapFilmic(vec3 c) {
    vec3 w = hablePartial(vec3(11.2));
    return clamp(hablePartial(c) / w, 0.0, 1.0);
}
vec3 tonemap(int op, vec3 c) {
    if (op == 1) return tonemapReinhard(c);
    if (op == 2) return tonemapFilmic(c);
    return tonemapAces(c);
}

void main() {
    vec3 color = texture(uScene, vUv).rgb;
    if (uBloomEnabled == 1) {
        color += texture(uBloom, vUv).rgb * uBloomIntensity;
    }
    color *= exp2(uExposure);
    color = tonemap(uTonemap, color);
    color = (color - vec3(0.18)) * uContrast + vec3(0.18);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, uSaturation);
    vec2 d = vUv - vec2(0.5);
    float vig = clamp(1.0 - uVignette * dot(d, d) * 4.0, 0.0, 1.0);
    color *= vig;
    color = pow(max(color, vec3(0.0)), vec3(1.0 / uGamma));
    FragColor = vec4(color, 1.0);
}

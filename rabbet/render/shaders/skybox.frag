in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uSkybox;
uniform int uHdrOutput; // 1: emit linear for the post pipeline; 0 (default): encode back to sRGB
// The exact inverse of the sampler's sRGB decode. A plain 1/2.2 power is not: it lifts
// the darkest codes (1 -> 6) and bands night gradients.
vec3 encodeSrgb(vec3 c) {
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), c));
}
void main() {
    vec3 color = texture(uSkybox, vDir).rgb;
    // Faces are display-referred, so the sky applies no tone curve of its own: the LDR
    // path just undoes the sampler's decode, keeping the direct look.
    if (uHdrOutput == 0) {
        color = encodeSrgb(clamp(color, 0.0, 1.0));
    }
    FragColor = vec4(color, 1.0);
}

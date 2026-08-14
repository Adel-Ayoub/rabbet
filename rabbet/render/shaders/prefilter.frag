layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 FragColor;
RB_FRAME_SAMPLER(0, sampler2D uScene)
RB_PER_DRAW_BEGIN
RB_PER_DRAW_AT(float uThreshold, 0)
RB_PER_DRAW_AT(float uKnee, 4)
RB_PER_DRAW_END
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    float brightness = max(c.r, max(c.g, c.b));
    float knee = uThreshold * uKnee + 1e-5;
    float soft = clamp(brightness - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, brightness - uThreshold) / max(brightness, 1e-5);
    FragColor = vec4(c * contribution, 1.0);
}

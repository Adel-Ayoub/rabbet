in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThreshold;
uniform float uKnee;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    float brightness = max(c.r, max(c.g, c.b));
    float knee = uThreshold * uKnee + 1e-5;
    float soft = clamp(brightness - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, brightness - uThreshold) / max(brightness, 1e-5);
    FragColor = vec4(c * contribution, 1.0);
}

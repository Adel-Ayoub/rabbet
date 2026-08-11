in vec3 vNormal;
in vec2 vUv;
uniform sampler2D uAlbedoTex;
uniform vec3 uBaseColor;
uniform int uHasTexture;
out vec4 FragColor;
void main() {
    vec3 albedo = uBaseColor;
    if (uHasTexture == 1) {
        albedo *= texture(uAlbedoTex, vUv).rgb;
    }
    vec3 n = normalize(vNormal);
    vec3 l = normalize(vec3(0.45, 0.8, 0.55));
    float diff = max(dot(n, l), 0.0);
    vec3 lit = albedo * (0.30 + 0.85 * diff);
    FragColor = vec4(pow(lit, vec3(1.0 / 2.2)), 1.0); // display-encode for the LDR ImGui pass
}

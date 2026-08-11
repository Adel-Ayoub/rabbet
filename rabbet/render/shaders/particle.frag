in vec2 vUv;
in vec4 vColor;
out vec4 FragColor;
uniform sampler2D uTexture;
void main() {
    vec4 texel = texture(uTexture, vUv);
    FragColor = vec4(texel.rgb * vColor.rgb, texel.a * vColor.a);
}

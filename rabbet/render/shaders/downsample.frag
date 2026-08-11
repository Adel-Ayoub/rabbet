in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uSource;
uniform vec2 uTexel;
void main() {
    vec2 t = uTexel;
    vec3 a = texture(uSource, vUv + t * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(uSource, vUv + t * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(uSource, vUv + t * vec2( 2.0, -2.0)).rgb;
    vec3 d = texture(uSource, vUv + t * vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(uSource, vUv).rgb;
    vec3 f = texture(uSource, vUv + t * vec2( 2.0,  0.0)).rgb;
    vec3 g = texture(uSource, vUv + t * vec2(-2.0,  2.0)).rgb;
    vec3 h = texture(uSource, vUv + t * vec2( 0.0,  2.0)).rgb;
    vec3 i = texture(uSource, vUv + t * vec2( 2.0,  2.0)).rgb;
    vec3 j = texture(uSource, vUv + t * vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(uSource, vUv + t * vec2( 1.0, -1.0)).rgb;
    vec3 l = texture(uSource, vUv + t * vec2(-1.0,  1.0)).rgb;
    vec3 m = texture(uSource, vUv + t * vec2( 1.0,  1.0)).rgb;
    vec3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;
    FragColor = vec4(result, 1.0);
}

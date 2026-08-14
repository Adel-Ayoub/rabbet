layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 FragColor;
RB_FRAME_SAMPLER(0, sampler2D uSource)
RB_PER_DRAW_BEGIN
RB_PER_DRAW_AT(vec2 uTexel, 0)
RB_PER_DRAW_END
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

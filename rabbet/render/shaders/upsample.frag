layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 FragColor;
RB_FRAME_SAMPLER(0, sampler2D uSource)
RB_PER_DRAW_BEGIN
RB_PER_DRAW_AT(vec2 uTexel, 0)
RB_PER_DRAW_AT(float uRadius, 8)
RB_PER_DRAW_END
void main() {
    vec2 t = uTexel * uRadius;
    vec3 result = texture(uSource, vUv).rgb * 4.0;
    result += texture(uSource, vUv + vec2(-t.x, 0.0)).rgb * 2.0;
    result += texture(uSource, vUv + vec2( t.x, 0.0)).rgb * 2.0;
    result += texture(uSource, vUv + vec2( 0.0, -t.y)).rgb * 2.0;
    result += texture(uSource, vUv + vec2( 0.0,  t.y)).rgb * 2.0;
    result += texture(uSource, vUv + vec2(-t.x, -t.y)).rgb;
    result += texture(uSource, vUv + vec2( t.x, -t.y)).rgb;
    result += texture(uSource, vUv + vec2(-t.x,  t.y)).rgb;
    result += texture(uSource, vUv + vec2( t.x,  t.y)).rgb;
    FragColor = vec4(result / 16.0, 1.0);
}

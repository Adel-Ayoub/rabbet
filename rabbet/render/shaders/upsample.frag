in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uSource;
uniform vec2 uTexel;
uniform float uRadius;
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

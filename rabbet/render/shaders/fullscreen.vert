layout(location = 0) out vec2 vUv;
void main() {
    vec2 p = vec2((RB_VERTEX_INDEX << 1) & 2, RB_VERTEX_INDEX & 2);
    vUv = vec2(p.x, RB_TEXTURE_Y(p.y));
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}

layout(location = 0) out vec4 FragColor;
RB_PER_DRAW_BEGIN
RB_PER_DRAW(mat4 uModel)
RB_PER_DRAW(vec3 uColor)
RB_PER_DRAW_END
void main() {
    FragColor = vec4(uColor, 1.0);
}

layout(location = 0) in vec3 aPosition;
RB_PER_FRAME_BEGIN
    mat4 uViewProjection;
RB_PER_FRAME_END
RB_PER_DRAW_BEGIN
RB_PER_DRAW_AT(mat4 uModel, 0)
RB_PER_DRAW_END
void main() {
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}

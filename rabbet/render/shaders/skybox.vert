layout(location = 0) in vec3 aPosition;
RB_PER_DRAW_BEGIN
RB_PER_DRAW_AT(mat4 uViewProjection, 0)
RB_PER_DRAW_END
layout(location = 0) out vec3 vDir;
void main() {
    vDir = aPosition;
    vec4 clip = uViewProjection * vec4(aPosition, 1.0);
    gl_Position = clip.xyww;
}

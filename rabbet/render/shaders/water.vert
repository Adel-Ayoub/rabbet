layout(location = 0) in vec3 aPosition;
RB_PER_FRAME_BEGIN
    mat4 uViewProjection;
RB_PER_FRAME_END
RB_PER_DRAW_BEGIN
RB_PER_DRAW_AT(mat4 uModel, 0)
RB_PER_DRAW_END
layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec2 vLocal;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vLocal = aPosition.xz; // unit-quad coords in [-1,1], scaled to world by uExtent below
    gl_Position = uViewProjection * world;
}

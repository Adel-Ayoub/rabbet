layout(location = 0) in vec3 aPosition;

uniform mat4 uViewProjection;
uniform mat4 uModel;

out vec3 vWorldPos;
out vec2 vLocal;

void main() {
    vec4 world = uModel * vec4(aPosition, 1.0);
    vWorldPos = world.xyz;
    vLocal = aPosition.xz; // unit-quad coords in [-1,1], scaled to world by uExtent below
    gl_Position = uViewProjection * world;
}

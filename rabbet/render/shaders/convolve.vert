layout(location = 0) in vec3 aPosition;
uniform mat4 uView;
uniform mat4 uProjection;
out vec3 vDir;
void main() {
    vDir = aPosition;
    gl_Position = uProjection * uView * vec4(aPosition, 1.0);
}

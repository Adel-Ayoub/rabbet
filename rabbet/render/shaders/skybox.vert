layout(location = 0) in vec3 aPosition;
uniform mat4 uViewProjection;
out vec3 vDir;
void main() {
    vDir = aPosition;
    vec4 clip = uViewProjection * vec4(aPosition, 1.0);
    gl_Position = clip.xyww;
}

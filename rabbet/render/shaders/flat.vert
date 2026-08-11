layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uViewProjection;
void main() {
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}

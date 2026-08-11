layout(location = 0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uLightSpace;
void main() {
    gl_Position = uLightSpace * uModel * vec4(aPosition, 1.0);
}

#VERTEX
#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUv;
uniform mat4 uModel;
uniform mat4 uViewProjection;
out vec2 vUv;
void main() {
    vUv = aUv;
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
}

#FRAGMENT
#version 410 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uAlbedoTex;
uniform vec3 uTint;
void main() {
    FragColor = vec4(texture(uAlbedoTex, vUv).rgb * uTint, 1.0);
}

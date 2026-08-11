uniform mat4 uInvViewProj;
out vec3 vNear;
out vec3 vFar;
void main() {
    vec2 uv = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vec2 ndc = uv * 2.0 - 1.0;
    vec4 nearPoint = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 farPoint = uInvViewProj * vec4(ndc, 1.0, 1.0);
    vNear = nearPoint.xyz / nearPoint.w;
    vFar = farPoint.xyz / farPoint.w;
    gl_Position = vec4(ndc, 0.0, 1.0);
}

in vec3 vDir;
out vec4 FragColor;
uniform samplerCube uEnvironment;
const float PI = 3.14159265359;
void main() {
    vec3 N = normalize(vDir);
    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up = normalize(cross(N, right));
    vec3 irradiance = vec3(0.0);
    float samples = 0.0;
    const float delta = 0.025;
    for (float phi = 0.0; phi < 2.0 * PI; phi += delta) {
        for (float theta = 0.0; theta < 0.5 * PI; theta += delta) {
            vec3 tangent = vec3(sin(theta) * cos(phi), sin(theta) * sin(phi), cos(theta));
            vec3 sampleDir = tangent.x * right + tangent.y * up + tangent.z * N;
            irradiance += texture(uEnvironment, sampleDir).rgb * cos(theta) * sin(theta);
            samples += 1.0;
        }
    }
    FragColor = vec4(PI * irradiance / samples, 1.0);
}

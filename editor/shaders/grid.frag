in vec3 vNear;
in vec3 vFar;
out vec4 FragColor;
uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform int uHdrOutput; // 1: emit linear for the post pipeline; 0 (default): display values as-is

float gridLine(vec2 pos, float spacing) {
    vec2 cell = pos / spacing;
    vec2 dist = abs(fract(cell - 0.5) - 0.5) / fwidth(cell);
    return 1.0 - clamp(min(dist.x, dist.y), 0.0, 1.0);
}

void main() {
    float denom = vFar.y - vNear.y;
    if (abs(denom) < 1e-6) {
        discard;
    }
    float t = -vNear.y / denom;
    if (t <= 0.0 || t >= 1.0) {
        discard;
    }
    vec3 world = mix(vNear, vFar, t);

    vec4 clip = uViewProj * vec4(world, 1.0);
    gl_FragDepth = clamp(clip.z / clip.w * 0.5 + 0.5, 0.0, 1.0);

    float minor = gridLine(world.xz, 1.0);
    float major = gridLine(world.xz, 10.0);

    vec3 color = vec3(0.42, 0.44, 0.47);
    float alpha = max(minor * 0.22, major * 0.45);

    // The world axes read as colored lines: x runs along z = 0, z along x = 0.
    if (abs(world.z) < fwidth(world.z)) {
        color = vec3(0.85, 0.35, 0.35);
        alpha = max(alpha, 0.8);
    }
    if (abs(world.x) < fwidth(world.x)) {
        color = vec3(0.35, 0.55, 0.9);
        alpha = max(alpha, 0.8);
    }

    float dist = length(world.xz - uCameraPos.xz);
    float fade = 1.0 - clamp((dist - 40.0) / 40.0, 0.0, 1.0);
    alpha *= fade;
    if (alpha <= 0.003) {
        discard;
    }
    if (uHdrOutput == 1) {
        color = pow(color, vec3(2.2)); // authored as display values; the post chain re-encodes
    }
    FragColor = vec4(color, alpha);
}

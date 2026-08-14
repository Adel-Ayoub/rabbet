#ifndef RB_DEPTH_TO_TEXTURE
#define RB_DEPTH_TO_TEXTURE(value) ((value) * 0.5 + 0.5)
#endif
#ifndef RB_TEXTURE_Y
#define RB_TEXTURE_Y(value) (value)
#endif

RB_FRAME_SAMPLER(3, sampler2D uShadowMap)
float shadowFactor(vec3 worldPos, vec3 N, vec3 L) {
    if (uHasShadowMap == 0) return 0.0;
    vec4 lightClip = uLightSpace * vec4(worldPos, 1.0);
    vec3 proj = lightClip.xyz / lightClip.w;
    proj.xy = proj.xy * 0.5 + 0.5;
    proj.y = RB_TEXTURE_Y(proj.y);
    proj.z = RB_DEPTH_TO_TEXTURE(proj.z);
    if (proj.z > 1.0) return 0.0;
    float bias = max(0.0025 * (1.0 - dot(N, L)), 0.0005);
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0));
    float shadow = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float depth = texture(uShadowMap, proj.xy + vec2(x, y) * texelSize).r;
            shadow += proj.z - bias > depth ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
}

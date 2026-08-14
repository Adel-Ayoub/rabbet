layout(location = 0) in vec2 vUv;
layout(location = 0) out vec4 FragColor;
RB_FRAME_SAMPLER(0, sampler2D uImage)
RB_PER_DRAW_BEGIN
RB_PER_DRAW_AT(vec2 uTexel, 0)
RB_PER_DRAW_END
float luma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }
void main() {
    vec3 rgbM = texture(uImage, vUv).rgb;
    float lM = luma(rgbM);
    float lNW = luma(texture(uImage, vUv + uTexel * vec2(-1.0, -1.0)).rgb);
    float lNE = luma(texture(uImage, vUv + uTexel * vec2( 1.0, -1.0)).rgb);
    float lSW = luma(texture(uImage, vUv + uTexel * vec2(-1.0,  1.0)).rgb);
    float lSE = luma(texture(uImage, vUv + uTexel * vec2( 1.0,  1.0)).rgb);
    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));
    vec2 dir = vec2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float reduce = max((lNW + lNE + lSW + lSE) * 0.25 * (1.0 / 8.0), 1.0 / 128.0);
    float rcpMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + reduce);
    dir = clamp(dir * rcpMin, -8.0, 8.0) * uTexel;
    vec3 rgbA = 0.5 * (texture(uImage, vUv + dir * (1.0 / 3.0 - 0.5)).rgb +
                       texture(uImage, vUv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 rgbB = rgbA * 0.5 + 0.25 * (texture(uImage, vUv + dir * -0.5).rgb +
                                     texture(uImage, vUv + dir * 0.5).rgb);
    float lB = luma(rgbB);
    FragColor = vec4((lB < lMin || lB > lMax) ? rgbA : rgbB, 1.0);
}

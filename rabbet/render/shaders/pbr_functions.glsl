float distributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = max(a * a, 1.0e-6);
    float nh = max(dot(N, H), 0.0);
    float d = nh * nh * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float geometrySchlick(float nv, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return nv / (nv * (1.0 - k) + k);
}
float geometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    return geometrySchlick(max(dot(N, V), 0.0), roughness) *
           geometrySchlick(max(dot(N, L), 0.0), roughness);
}
vec3 fresnelSchlick(float cosTheta, vec3 f0) {
    return f0 + (1.0 - f0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}
vec3 brdf(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo, vec3 f0) {
    vec3 H = normalize(V + L);
    float ndf = distributionGGX(N, H, uRoughness);
    float g = geometrySmith(N, V, L, uRoughness);
    vec3 f = fresnelSchlick(max(dot(H, V), 0.0), f0);
    vec3 specular = (ndf * g * f) /
                    (4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001);
    vec3 kd = (vec3(1.0) - f) * (1.0 - uMetallic);
    return (kd * albedo / PI + specular) * radiance * max(dot(N, L), 0.0);
}

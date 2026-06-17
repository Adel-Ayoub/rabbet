#include "rabbet/render/PostProcessor.h"

#include "rabbet/render/PostProcess.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>

namespace rb {
namespace {

// One vertex shader for every pass: a single oversized triangle generated from gl_VertexID, so no
// VBO is needed. vUv covers [0,1] across the screen.
constexpr const char* kFullscreenVertex = R"(#version 410 core
out vec2 vUv;
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vUv = p;
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Soft-knee bright-pass: keeps only the part of each pixel above the threshold, easing in over a
// knee so the bloom doesn't pop on hard edges.
constexpr const char* kPrefilterFragment = R"(#version 410 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform float uThreshold;
uniform float uKnee;
void main() {
    vec3 c = texture(uScene, vUv).rgb;
    float brightness = max(c.r, max(c.g, c.b));
    float knee = uThreshold * uKnee + 1e-5;
    float soft = clamp(brightness - uThreshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, brightness - uThreshold) / max(brightness, 1e-5);
    FragColor = vec4(c * contribution, 1.0);
}
)";

// 13-tap downsample (Jimenez / "Next Generation Post Processing in Call of Duty: Advanced Warfare")
// — a stable, firefly-resistant box of weighted taps.
constexpr const char* kDownsampleFragment = R"(#version 410 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uSource;
uniform vec2 uTexel;
void main() {
    vec2 t = uTexel;
    vec3 a = texture(uSource, vUv + t * vec2(-2.0, -2.0)).rgb;
    vec3 b = texture(uSource, vUv + t * vec2( 0.0, -2.0)).rgb;
    vec3 c = texture(uSource, vUv + t * vec2( 2.0, -2.0)).rgb;
    vec3 d = texture(uSource, vUv + t * vec2(-2.0,  0.0)).rgb;
    vec3 e = texture(uSource, vUv).rgb;
    vec3 f = texture(uSource, vUv + t * vec2( 2.0,  0.0)).rgb;
    vec3 g = texture(uSource, vUv + t * vec2(-2.0,  2.0)).rgb;
    vec3 h = texture(uSource, vUv + t * vec2( 0.0,  2.0)).rgb;
    vec3 i = texture(uSource, vUv + t * vec2( 2.0,  2.0)).rgb;
    vec3 j = texture(uSource, vUv + t * vec2(-1.0, -1.0)).rgb;
    vec3 k = texture(uSource, vUv + t * vec2( 1.0, -1.0)).rgb;
    vec3 l = texture(uSource, vUv + t * vec2(-1.0,  1.0)).rgb;
    vec3 m = texture(uSource, vUv + t * vec2( 1.0,  1.0)).rgb;
    vec3 result = e * 0.125;
    result += (a + c + g + i) * 0.03125;
    result += (b + d + f + h) * 0.0625;
    result += (j + k + l + m) * 0.125;
    FragColor = vec4(result, 1.0);
}
)";

// 3x3 tent upsample, blended additively into the next-larger mip.
constexpr const char* kUpsampleFragment = R"(#version 410 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uSource;
uniform vec2 uTexel;
uniform float uRadius;
void main() {
    vec2 t = uTexel * uRadius;
    vec3 result = texture(uSource, vUv).rgb * 4.0;
    result += texture(uSource, vUv + vec2(-t.x, 0.0)).rgb * 2.0;
    result += texture(uSource, vUv + vec2( t.x, 0.0)).rgb * 2.0;
    result += texture(uSource, vUv + vec2( 0.0, -t.y)).rgb * 2.0;
    result += texture(uSource, vUv + vec2( 0.0,  t.y)).rgb * 2.0;
    result += texture(uSource, vUv + vec2(-t.x, -t.y)).rgb;
    result += texture(uSource, vUv + vec2( t.x, -t.y)).rgb;
    result += texture(uSource, vUv + vec2(-t.x,  t.y)).rgb;
    result += texture(uSource, vUv + vec2( t.x,  t.y)).rgb;
    FragColor = vec4(result / 16.0, 1.0);
}
)";

// Composite: combine scene + bloom, expose, tone-map, grade, gamma. The tone-map curves mirror
// rabbet/render/Tonemap.h (unit-tested on the CPU) so the GPU and the tests agree.
constexpr const char* kCompositeFragment = R"(#version 410 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uScene;
uniform sampler2D uBloom;
uniform int uBloomEnabled;
uniform float uBloomIntensity;
uniform float uExposure;
uniform int uTonemap;
uniform float uGamma;
uniform float uContrast;
uniform float uSaturation;
uniform float uVignette;

vec3 tonemapReinhard(vec3 c) { return c / (c + vec3(1.0)); }
vec3 tonemapAces(vec3 x) {
    const float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
vec3 hablePartial(vec3 x) {
    const float a = 0.15, b = 0.50, c = 0.10, d = 0.20, e = 0.02, f = 0.30;
    return ((x * (a * x + c * b) + d * e) / (x * (a * x + b) + d * f)) - e / f;
}
vec3 tonemapFilmic(vec3 c) {
    vec3 w = hablePartial(vec3(11.2));
    return clamp(hablePartial(c) / w, 0.0, 1.0);
}
vec3 tonemap(int op, vec3 c) {
    if (op == 1) return tonemapReinhard(c);
    if (op == 2) return tonemapFilmic(c);
    return tonemapAces(c);
}

void main() {
    vec3 color = texture(uScene, vUv).rgb;
    if (uBloomEnabled == 1) {
        color += texture(uBloom, vUv).rgb * uBloomIntensity;
    }
    color *= exp2(uExposure);
    color = tonemap(uTonemap, color);
    color = (color - vec3(0.18)) * uContrast + vec3(0.18);
    float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
    color = mix(vec3(luma), color, uSaturation);
    vec2 d = vUv - vec2(0.5);
    float vig = clamp(1.0 - uVignette * dot(d, d) * 4.0, 0.0, 1.0);
    color *= vig;
    color = pow(max(color, vec3(0.0)), vec3(1.0 / uGamma));
    FragColor = vec4(color, 1.0);
}
)";

// FXAA (Lottes' classic console variant): edge-directed blur driven by luma, on the LDR image.
constexpr const char* kFxaaFragment = R"(#version 410 core
in vec2 vUv;
out vec4 FragColor;
uniform sampler2D uImage;
uniform vec2 uTexel;
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
)";

void bindRawTexture(unsigned int texture, unsigned int unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, texture);
}

glm::vec2 texelSize(const gl::ColorTarget& target) {
    return glm::vec2(1.0f / static_cast<float>(target.width()),
                     1.0f / static_cast<float>(target.height()));
}

} // namespace

void PostProcessor::init() {
    glGenVertexArrays(1, &m_vao);
    m_prefilter = gl::Shader::fromSource(kFullscreenVertex, kPrefilterFragment);
    m_downsample = gl::Shader::fromSource(kFullscreenVertex, kDownsampleFragment);
    m_upsample = gl::Shader::fromSource(kFullscreenVertex, kUpsampleFragment);
    m_composite = gl::Shader::fromSource(kFullscreenVertex, kCompositeFragment);
    m_fxaa = gl::Shader::fromSource(kFullscreenVertex, kFxaaFragment);
    if (!m_prefilter || !m_downsample || !m_upsample || !m_composite || !m_fxaa) {
        log::error("post-processor: failed to build a post-process shader");
    }
}

void PostProcessor::ensureTargets(int width, int height) {
    const int w = std::max(width, 1);
    const int h = std::max(height, 1);
    if (w == m_width && h == m_height && !m_bloom.empty()) {
        return;
    }
    m_width = w;
    m_height = h;

    // Bloom mip chain, starting at half resolution and halving until a small floor (or a cap), so
    // the glow is wide and cheap. At least one level always exists.
    m_bloom.clear();
    int mw = std::max(w / 2, 1);
    int mh = std::max(h / 2, 1);
    constexpr int kMaxLevels = 6;
    for (int level = 0; level < kMaxLevels; ++level) {
        m_bloom.push_back(gl::ColorTarget::create(mw, mh, true));
        if (mw <= 2 || mh <= 2) {
            break;
        }
        mw = std::max(mw / 2, 1);
        mh = std::max(mh / 2, 1);
    }

    m_ldr = gl::ColorTarget::create(w, h, false);
    m_aa = gl::ColorTarget::create(w, h, false);
}

void PostProcessor::drawFullscreen() const noexcept {
    glBindVertexArray(m_vao);
    glDrawArrays(GL_TRIANGLES, 0, 3);
}

unsigned int PostProcessor::process(unsigned int hdrTexture, int width, int height,
                                    const PostProcess& settings) {
    ensureTargets(width, height);
    if (!m_composite || !m_prefilter || !m_downsample || !m_upsample || m_bloom.empty() ||
        !m_ldr || !m_aa) {
        return hdrTexture; // shaders/targets unavailable: fall back to the raw HDR texture
    }

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);

    // --- Bloom: prefilter -> downsample chain -> additive upsample back to m_bloom[0]. ---
    if (settings.bloom) {
        m_bloom[0].bind();
        m_prefilter->bind();
        bindRawTexture(hdrTexture, 0);
        m_prefilter->setInt("uScene", 0);
        m_prefilter->setFloat("uThreshold", settings.bloomThreshold);
        m_prefilter->setFloat("uKnee", settings.bloomKnee);
        drawFullscreen();

        m_downsample->bind();
        m_downsample->setInt("uSource", 0);
        for (std::size_t i = 0; i + 1 < m_bloom.size(); ++i) {
            m_bloom[i + 1].bind();
            m_bloom[i].bindTexture(0);
            m_downsample->setVec2("uTexel", texelSize(m_bloom[i]));
            drawFullscreen();
        }

        glEnable(GL_BLEND);
        glBlendFunc(GL_ONE, GL_ONE);
        m_upsample->bind();
        m_upsample->setInt("uSource", 0);
        m_upsample->setFloat("uRadius", 1.0f);
        for (std::size_t i = m_bloom.size() - 1; i > 0; --i) {
            m_bloom[i - 1].bind();
            m_bloom[i].bindTexture(0);
            m_upsample->setVec2("uTexel", texelSize(m_bloom[i]));
            drawFullscreen();
        }
        glDisable(GL_BLEND);
    }

    // --- Composite: scene (+ bloom) -> expose -> tone-map -> grade -> gamma -> LDR. ---
    m_ldr->bind();
    m_composite->bind();
    bindRawTexture(hdrTexture, 0);
    m_composite->setInt("uScene", 0);
    m_bloom[0].bindTexture(1);
    m_composite->setInt("uBloom", 1);
    m_composite->setInt("uBloomEnabled", settings.bloom ? 1 : 0);
    m_composite->setFloat("uBloomIntensity", settings.bloomIntensity);
    m_composite->setFloat("uExposure", settings.exposure);
    m_composite->setInt("uTonemap", static_cast<int>(settings.tonemap));
    m_composite->setFloat("uGamma", settings.gamma);
    m_composite->setFloat("uContrast", settings.contrast);
    m_composite->setFloat("uSaturation", settings.saturation);
    m_composite->setFloat("uVignette", settings.vignette);
    drawFullscreen();

    unsigned int result = m_ldr->texture();

    // --- FXAA on the LDR image. ---
    if (settings.fxaa) {
        m_aa->bind();
        m_fxaa->bind();
        m_ldr->bindTexture(0);
        m_fxaa->setInt("uImage", 0);
        m_fxaa->setVec2("uTexel", glm::vec2(1.0f / static_cast<float>(m_width),
                                            1.0f / static_cast<float>(m_height)));
        drawFullscreen();
        result = m_aa->texture();
    }

    glBindVertexArray(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    return result;
}

PostProcessor::~PostProcessor() {
    if (m_vao != 0) {
        glDeleteVertexArrays(1, &m_vao);
    }
}

} // namespace rb

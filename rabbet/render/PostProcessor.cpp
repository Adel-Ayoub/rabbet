#include "rabbet/render/PostProcessor.h"

#include "rabbet/render/PostProcess.h"
#include "rabbet/render/shaders/GlShaderSources.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <algorithm>

namespace rb {
namespace {

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
    // The shared vertex stage builds one oversized triangle from gl_VertexID, so no
    // vertex buffer exists anywhere in the chain. Bloom starts from a soft knee bright
    // pass, walks down through the thirteen tap weighted box from Jimenez and returns
    // through a three by three tent blended additively. FXAA is the classic Lottes
    // console variant applied to the LDR image. The composite curves mirror
    // rabbet/render/Tonemap.h, which the CPU tests pin, so the GPU pass and the suite
    // keep agreeing.
    m_prefilter = gl::Shader::fromSource(shaders::kFullscreenVertex, shaders::kPrefilterFragment);
    m_downsample = gl::Shader::fromSource(shaders::kFullscreenVertex, shaders::kDownsampleFragment);
    m_upsample = gl::Shader::fromSource(shaders::kFullscreenVertex, shaders::kUpsampleFragment);
    m_composite = gl::Shader::fromSource(shaders::kFullscreenVertex, shaders::kCompositeFragment);
    m_fxaa = gl::Shader::fromSource(shaders::kFullscreenVertex, shaders::kFxaaFragment);
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

    // The composite/FXAA textures are what the viewport image displays: resize them in
    // place so their GL ids stay stable, because ImGui recorded this frame's draw list
    // with the OLD id before the scene render ran (recreating here left one deleted-name
    // frame per resize step, a continuous flicker during a drag). The bloom chain is
    // internal to this pass and its level count varies with size, so it recreates.
    if (m_ldr && m_aa) {
        m_ldr->resize(w, h);
        m_aa->resize(w, h);
    } else {
        m_ldr = gl::ColorTarget::create(w, h, false);
        m_aa = gl::ColorTarget::create(w, h, false);
    }
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

    // --- FXAA on the LDR image (skipped if its shader failed to compile at init). ---
    if (settings.fxaa && m_fxaa) {
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

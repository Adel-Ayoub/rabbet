#include "rabbet/render/SkyboxSystem.h"

#include "rabbet/assets/AssetDatabase.h"
#include "rabbet/core/Runtime.h"
#include "rabbet/ecs/Scene.h"
#include "rabbet/render/EnvironmentLighting.h"
#include "rabbet/render/Image.h"
#include "rabbet/render/ImageLoader.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/render/RenderView.h"
#include "rabbet/render/Skybox.h"
#include "rabbet/render/SkyboxComponent.h"
#include "rabbet/render/shaders/GlShaderSources.h"
#include "rabbet/util/Log.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <array>
#include <optional>
#include <string>

namespace rb {

void SkyboxSystem::onStart(Runtime&) {
    m_shader = gl::Shader::fromSource(shaders::kSkyboxVertex, shaders::kSkyboxFragment);
    if (!m_shader) {
        log::error("skybox system: failed to build the skybox shader");
    }
}

void SkyboxSystem::reconcile(Runtime& runtime) {
    // The scene's own sky, else the host's default, so a scene without one still gets a
    // deterministic sky instead of inheriting whatever was on screen before it.
    const SkyboxComponent* sky = activeSkybox(runtime.scene());
    std::array<Uuid, 6> wanted{};
    if (sky != nullptr) {
        wanted = sky->faces;
    } else if (runtime.hasResource<SkyboxDefault>()) {
        wanted = runtime.resource<SkyboxDefault>().faces;
    } else {
        return; // nothing to reconcile against; leave the sky alone
    }

    if (wanted == m_loaded) {
        return;
    }
    // A half-filled component is mid-authoring, not an error: a freshly added skybox has
    // six empty slots and the user assigns them one at a time.
    for (const Uuid& face : wanted) {
        if (!face.valid()) {
            return;
        }
    }

    AssetDatabase* database = runtime.tryResource<AssetDatabase>();
    if (database == nullptr) {
        return;
    }

    // A broken set is retried on later frames (the file may still be arriving, or the user
    // is about to fix the assignment), so the failure paths must not touch m_loaded, which
    // is the retry gate. m_warned only keeps the log to one line per set.
    const auto refuse = [this, &wanted](const std::string& why) {
        if (m_warned != wanted) {
            m_warned = wanted;
            log::warn("skybox: not switching, {}", why);
        }
    };

    // All six have to load before anything is swapped: a half-loaded cubemap would be
    // worse than the sky already on screen.
    std::array<Image, 6> images;
    for (std::size_t i = 0; i < images.size(); ++i) {
        const AssetDatabase::Record* record = database->find(wanted[i]);
        if (record == nullptr) {
            refuse("face " + std::to_string(i) + " is not a catalogued texture");
            return;
        }
        std::optional<Image> image = loadImage(record->path, false);
        if (!image) {
            refuse("could not read '" + record->path.string() + "'");
            return;
        }
        images[i] = std::move(*image);
    }

    // Cubemap upload takes its pixel format from the first face and applies it to all six,
    // so a face that disagrees would be read past the end of its buffer. Faces must also
    // be square and equal-sized or the cubemap is incomplete and samples black.
    const Image& first = images.front();
    if (first.width != first.height) {
        refuse("faces must be square");
        return;
    }
    // The cubemap declares colour faces sRGB and the shader undoes that decode on the
    // direct path; a single-channel face never decodes, so it would re-encode wrongly.
    if (first.channels < 3) {
        refuse("faces must be RGB or RGBA");
        return;
    }
    for (std::size_t i = 1; i < images.size(); ++i) {
        if (images[i].width != first.width || images[i].height != first.height ||
            images[i].channels != first.channels) {
            refuse("face " + std::to_string(i) + " does not match face 0 in size or channels");
            return;
        }
    }

    Skybox& skybox = runtime.resource<Skybox>();
    skybox.cubemap = gl::Cubemap::fromFaces(images);
    m_loaded = wanted;

    // The ambient probe was convolved from the old sky, so it has to follow or a night
    // scene keeps lighting its surfaces with the previous sky's colour. Keep the user's
    // enabled/intensity choices across the rebuild.
    if (runtime.hasResource<EnvironmentLight>()) {
        EnvironmentLight& env = runtime.resource<EnvironmentLight>();
        const bool enabled = env.enabled;
        const float intensity = env.intensity;
        env = buildEnvironmentLight(skybox.cubemap);
        env.enabled = enabled;
        env.intensity = intensity;
    }
    log::info("skybox: rebuilt from the scene's skybox");
}

void SkyboxSystem::onUpdate(Runtime& runtime, float) {
    if (!m_shader || !runtime.hasResource<RenderView>() || !runtime.hasResource<Skybox>()) {
        return;
    }
    reconcile(runtime);

    const RenderView& view = runtime.resource<RenderView>();
    const Skybox& skybox = runtime.resource<Skybox>();

    const glm::mat4 rotationOnlyView = glm::mat4(glm::mat3(view.view));
    const glm::mat4 viewProjection = view.projection * rotationOnlyView;

    glDepthFunc(GL_LEQUAL);
    m_shader->bind();
    m_shader->setMat4("uViewProjection", viewProjection);
    m_shader->setInt("uSkybox", 0);
    m_shader->setInt("uHdrOutput", activePostProcess(runtime.scene()) != nullptr ? 1 : 0);
    skybox.cubemap.bind(0);
    skybox.mesh.draw();
    glDepthFunc(GL_LESS);
}

} // namespace rb

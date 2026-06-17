#include "rabbet/ecs/Scene.h"
#include "rabbet/render/PostProcess.h"
#include "rabbet/render/Tonemap.h"
#include "rabbet/serialize/BuiltinComponents.h"
#include "rabbet/serialize/ComponentRegistry.h"
#include "rabbet/serialize/SceneSerializer.h"
#include "tests/Test.h"

#include <glm/glm.hpp>

#include <cmath>

namespace {

bool approx(float a, float b, float eps = 1.0e-4f) {
    return std::fabs(a - b) <= eps;
}

// Exposure scales linearly in stops: +1 EV doubles, -1 EV halves, 0 EV is identity.
void exposureScalesInStops() {
    const glm::vec3 c(0.4f, 0.6f, 0.8f);
    CHECK(approx(rb::applyExposure(c, 0.0f).r, 0.4f));
    CHECK(approx(rb::applyExposure(c, 1.0f).g, 1.2f));
    CHECK(approx(rb::applyExposure(c, -1.0f).b, 0.4f));
}

void luminanceUsesRec709Weights() {
    CHECK(approx(rb::luminance(glm::vec3(1.0f)), 1.0f));      // weights sum to 1
    CHECK(approx(rb::luminance(glm::vec3(0.0f)), 0.0f));
    CHECK(approx(rb::luminance(glm::vec3(0.0f, 1.0f, 0.0f)), 0.7152f)); // green dominates
    CHECK(rb::luminance(glm::vec3(0, 1, 0)) > rb::luminance(glm::vec3(1, 0, 0)));
}

// ACES maps 0 -> 0, is monotonic, and saturates toward (but not past) 1.
void acesIsBlackPreservingAndBounded() {
    CHECK(approx(rb::tonemapAces(glm::vec3(0.0f)).r, 0.0f));
    const float low = rb::tonemapAces(glm::vec3(0.2f)).r;
    const float mid = rb::tonemapAces(glm::vec3(1.0f)).r;
    const float high = rb::tonemapAces(glm::vec3(8.0f)).r;
    CHECK(low < mid);
    CHECK(mid < high);
    CHECK(high <= 1.0f);
    CHECK(high > 0.9f); // bright HDR approaches white
}

void reinhardMatchesClosedForm() {
    CHECK(approx(rb::tonemapReinhard(glm::vec3(0.0f)).r, 0.0f));
    CHECK(approx(rb::tonemapReinhard(glm::vec3(1.0f)).r, 0.5f)); // x/(x+1)
    CHECK(approx(rb::tonemapReinhard(glm::vec3(3.0f)).r, 0.75f));
    CHECK(rb::tonemapReinhard(glm::vec3(1000.0f)).r < 1.0f);
}

void filmicIsBlackPreservingAndBounded() {
    CHECK(approx(rb::tonemapFilmic(glm::vec3(0.0f)).r, 0.0f, 1.0e-3f));
    const float mid = rb::tonemapFilmic(glm::vec3(1.0f)).r;
    const float high = rb::tonemapFilmic(glm::vec3(11.2f)).r;
    CHECK(mid < high);
    CHECK(high <= 1.0f);
    CHECK(high > 0.95f); // the curve's white point
}

void tonemapDispatchSelectsOperator() {
    const glm::vec3 c(1.0f);
    CHECK(approx(rb::tonemap(rb::TonemapOperator::Reinhard, c).r, 0.5f));
    CHECK(approx(rb::tonemap(rb::TonemapOperator::ACES, c).r, rb::tonemapAces(c).r));
    CHECK(approx(rb::tonemap(rb::TonemapOperator::Filmic, c).r, rb::tonemapFilmic(c).r));
}

// Gamma encode then decode round-trips; gamma 1.0 is identity.
void gammaRoundTrips() {
    const glm::vec3 c(0.25f, 0.5f, 0.75f);
    CHECK(approx(rb::gammaCorrect(c, 1.0f).r, 0.25f));
    const glm::vec3 encoded = rb::gammaCorrect(c, 2.2f);
    CHECK(encoded.r > c.r); // 1/2.2 power brightens midtones
    const glm::vec3 decoded = glm::pow(encoded, glm::vec3(2.2f));
    CHECK(approx(decoded.r, 0.25f));
    CHECK(approx(decoded.b, 0.75f));
}

// Contrast pivots around 0.18: identity at 1.0, pushes away from mid-grey above it.
void contrastPivotsAtMidGrey() {
    CHECK(approx(rb::applyContrast(glm::vec3(0.7f), 1.0f).r, 0.7f));
    CHECK(approx(rb::applyContrast(glm::vec3(0.18f), 2.0f).r, 0.18f)); // mid-grey is fixed
    CHECK(rb::applyContrast(glm::vec3(0.5f), 1.5f).r > 0.5f);          // above mid pushed up
    CHECK(rb::applyContrast(glm::vec3(0.05f), 1.5f).r < 0.05f);        // below mid pushed down
}

// Saturation 1.0 is identity; 0.0 collapses to luma grey; values agree on the grey axis.
void saturationInterpolatesToLuma() {
    const glm::vec3 c(0.8f, 0.2f, 0.4f);
    const glm::vec3 ident = rb::applySaturation(c, 1.0f);
    CHECK(approx(ident.r, 0.8f));
    CHECK(approx(ident.g, 0.2f));
    const glm::vec3 grey = rb::applySaturation(c, 0.0f);
    const float l = rb::luminance(c);
    CHECK(approx(grey.r, l));
    CHECK(approx(grey.g, l));
    CHECK(approx(grey.b, l));
}

// activePostProcess returns the first ENABLED instance, skipping disabled ones; none -> nullptr.
void activePostProcessPicksFirstEnabled() {
    rb::Scene scene;
    const rb::Entity a = scene.create();
    rb::PostProcess off;
    off.enabled = false;
    scene.add<rb::PostProcess>(a, off);
    CHECK(rb::activePostProcess(scene) == nullptr); // a disabled one is inert

    const rb::Entity b = scene.create();
    rb::PostProcess on;
    on.enabled = true;
    on.exposure = 1.5f;
    scene.add<rb::PostProcess>(b, on);
    rb::PostProcess* active = rb::activePostProcess(scene);
    CHECK(active != nullptr);
    if (active != nullptr) {
        CHECK(approx(active->exposure, 1.5f));
    }
}

// The PostProcess component round-trips through the scene serializer with every field intact.
void postProcessRoundTrips() {
    rb::ComponentRegistry registry;
    rb::registerBuiltinComponents(registry);

    rb::Scene source;
    const rb::Entity e = source.create();
    rb::PostProcess pp;
    pp.enabled = true;
    pp.tonemap = rb::TonemapOperator::Filmic;
    pp.exposure = 0.75f;
    pp.gamma = 2.4f;
    pp.bloom = true;
    pp.bloomThreshold = 1.3f;
    pp.bloomKnee = 0.6f;
    pp.bloomIntensity = 0.09f;
    pp.contrast = 1.15f;
    pp.saturation = 0.85f;
    pp.vignette = 0.3f;
    pp.fxaa = false;
    source.add<rb::PostProcess>(e, pp);

    const nlohmann::json doc = rb::SceneSerializer::toJson(source, registry);
    rb::Scene loaded;
    rb::SceneSerializer::fromJson(doc, loaded, registry);

    CHECK(loaded.count<rb::PostProcess>() == 1u);
    loaded.each<rb::PostProcess>([&](rb::Entity, rb::PostProcess& l) {
        CHECK(l.enabled == true);
        CHECK(l.tonemap == rb::TonemapOperator::Filmic);
        CHECK(approx(l.exposure, 0.75f));
        CHECK(approx(l.gamma, 2.4f));
        CHECK(approx(l.bloomThreshold, 1.3f));
        CHECK(approx(l.bloomIntensity, 0.09f));
        CHECK(approx(l.contrast, 1.15f));
        CHECK(approx(l.saturation, 0.85f));
        CHECK(approx(l.vignette, 0.3f));
        CHECK(l.fxaa == false);
    });
}

} // namespace

int main() {
    exposureScalesInStops();
    luminanceUsesRec709Weights();
    acesIsBlackPreservingAndBounded();
    reinhardMatchesClosedForm();
    filmicIsBlackPreservingAndBounded();
    tonemapDispatchSelectsOperator();
    gammaRoundTrips();
    contrastPivotsAtMidGrey();
    saturationInterpolatesToLuma();
    activePostProcessPicksFirstEnabled();
    postProcessRoundTrips();
    return rbtest::summary("tonemap");
}

#pragma once

namespace rb {

enum class TextureFilter {
    Nearest,
    Linear,
};

enum class TextureAddress {
    Repeat,
    ClampToEdge,
};

struct SamplerConfig {
    TextureFilter minFilter = TextureFilter::Linear;
    TextureFilter magFilter = TextureFilter::Linear;
    TextureFilter mipFilter = TextureFilter::Linear;
    TextureAddress address = TextureAddress::Repeat;
};

struct TextureConfig {
    bool srgb = false;
    bool generateMipmaps = true;
    SamplerConfig sampler;
};

} // namespace rb

#include "rabbet/render/ImageLoader.h"

#include "rabbet/util/Log.h"

#include <stb_image.h>

#include <cstring>

namespace rb {

std::optional<Image> loadImage(const std::filesystem::path& path, bool flipVertically) {
    stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* data = stbi_load(path.string().c_str(), &width, &height, &channels, 0);
    if (data == nullptr) {
        log::error("failed to load image '{}': {}", path.string(), stbi_failure_reason());
        return std::nullopt;
    }
    if (channels == 2 || channels == 3) {
        // Sampled backends share R8 and RGBA8. Preserve grey as R8 and expand the
        // remaining byte formats so every upload uses a portable pixel layout.
        stbi_image_free(data);
        data = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
        if (data == nullptr) {
            log::error("failed to load image '{}': {}", path.string(), stbi_failure_reason());
            return std::nullopt;
        }
        channels = 4;
    }

    Image image;
    image.width = width;
    image.height = height;
    image.channels = channels;

    const auto byteCount = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) *
                           static_cast<std::size_t>(channels);
    image.pixels.resize(byteCount);
    std::memcpy(image.pixels.data(), data, byteCount);
    stbi_image_free(data);

    return image;
}

} // namespace rb

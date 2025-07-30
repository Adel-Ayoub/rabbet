#include "rabbet/render/ImageLoader.h"
#include "tests/Test.h"

#include <filesystem>
#include <fstream>
#include <ios>

namespace {

std::filesystem::path writeTempPpm() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_test_image.ppm";
    std::ofstream out(path, std::ios::binary);
    out << "P6\n2 2\n255\n";
    const unsigned char pixels[] = {255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};
    out.write(reinterpret_cast<const char*>(pixels), static_cast<std::streamsize>(sizeof(pixels)));
    return path;
}

} // namespace

static void loadsPpm() {
    const std::filesystem::path path = writeTempPpm();
    const std::optional<rb::Image> image = rb::loadImage(path, false);
    std::filesystem::remove(path);

    CHECK(image.has_value());
    if (image) {
        CHECK(image->width == 2);
        CHECK(image->height == 2);
        CHECK(image->channels == 3);
        CHECK(image->pixels.size() == 12u);
    }
}

static void missingFileReturnsNullopt() {
    const std::optional<rb::Image> image = rb::loadImage("/no/such/rabbet_missing_image.ppm");
    CHECK(!image.has_value());
}

int main() {
    loadsPpm();
    missingFileReturnsNullopt();
    return rbtest::summary("render_image_loader");
}

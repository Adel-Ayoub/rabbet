#include "rabbet/render/ModelLoader.h"
#include "tests/Test.h"

#include <filesystem>
#include <fstream>

namespace {

std::filesystem::path writeTempObj() {
    const std::filesystem::path path =
        std::filesystem::temp_directory_path() / "rabbet_test_triangle.obj";
    std::ofstream out(path);
    out << "v 0 0 0\n"
           "v 1 0 0\n"
           "v 0 1 0\n"
           "vn 0 0 1\n"
           "vt 0 0\n"
           "vt 1 0\n"
           "vt 0 1\n"
           "f 1/1/1 2/2/1 3/3/1\n";
    return path;
}

} // namespace

static void loadsTriangleObj() {
    const std::filesystem::path path = writeTempObj();
    const std::optional<rb::Model> model = rb::loadModel(path);
    std::filesystem::remove(path);

    CHECK(model.has_value());
    if (model) {
        CHECK(model->meshes.size() == 1u);
        CHECK(model->meshes.front().data.vertices.size() == 3u);
        CHECK(model->meshes.front().data.indices.size() == 3u);
        CHECK(!model->materials.empty());
    }
}

static void missingFileReturnsNullopt() {
    const std::optional<rb::Model> model = rb::loadModel("/no/such/rabbet_missing_model.obj");
    CHECK(!model.has_value());
}

int main() {
    loadsTriangleObj();
    missingFileReturnsNullopt();
    return rbtest::summary("render_model_loader");
}

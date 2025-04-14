add_library(rabbet)
add_library(Rabbet::Rabbet ALIAS rabbet)

target_sources(rabbet
    PRIVATE
        platform/Window.cpp
    PUBLIC
        FILE_SET rb_headers TYPE HEADERS
        BASE_DIRS "${PROJECT_SOURCE_DIR}"
        FILES
            platform/Window.h)

target_include_directories(rabbet PUBLIC
    "${PROJECT_SOURCE_DIR}")

target_link_libraries(rabbet
    PUBLIC
        glad
        glfw
        OpenGL::GL)

target_compile_features(rabbet PUBLIC cxx_std_20)

rb_target_warnings(rabbet)

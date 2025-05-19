add_library(rabbet)
add_library(Rabbet::Rabbet ALIAS rabbet)

target_sources(rabbet
    PRIVATE
        platform/Window.cpp
        ecs/Scene.cpp
        core/Runtime.cpp
    PUBLIC
        FILE_SET rb_headers TYPE HEADERS
        BASE_DIRS "${PROJECT_SOURCE_DIR}"
        FILES
            platform/Window.h
            ecs/Entity.h
            ecs/Component.h
            ecs/Pool.h
            ecs/Scene.h
            core/TypeId.h
            core/System.h
            core/Resource.h
            core/Module.h
            core/Runtime.h)

target_include_directories(rabbet PUBLIC
    "${PROJECT_SOURCE_DIR}")

target_link_libraries(rabbet
    PUBLIC
        glad
        glfw
        OpenGL::GL)

target_compile_features(rabbet PUBLIC cxx_std_20)

rb_target_warnings(rabbet)

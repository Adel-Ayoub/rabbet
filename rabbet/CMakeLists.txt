add_library(rabbet)
add_library(Rabbet::Rabbet ALIAS rabbet)

target_sources(rabbet
    PRIVATE
        platform/Window.cpp
        platform/Input.cpp
        ecs/Scene.cpp
        core/Runtime.cpp
        scene/TransformSystem.cpp
        render/Geometry.cpp
        render/RenderDevice.cpp
        render/gl/Device.cpp
        render/gl/Shader.cpp
        render/gl/Texture.cpp
        render/gl/Mesh.cpp
        render/RenderSystem.cpp
        scene/CameraSystem.cpp
        scene/LightSystem.cpp
        render/ImageLoader.cpp
        render/ModelLoader.cpp
        util/Log.cpp
    PUBLIC
        FILE_SET rb_headers TYPE HEADERS
        BASE_DIRS "${PROJECT_SOURCE_DIR}"
        FILES
            platform/Window.h
            platform/Input.h
            ecs/Entity.h
            ecs/Component.h
            ecs/Pool.h
            ecs/Scene.h
            core/TypeId.h
            core/System.h
            core/Resource.h
            core/Module.h
            core/Runtime.h
            core/Clock.h
            scene/Transform.h
            scene/WorldMatrix.h
            scene/TransformSystem.h
            render/Vertex.h
            render/MeshData.h
            render/Geometry.h
            render/RenderDevice.h
            render/gl/Device.h
            render/gl/Shader.h
            render/gl/Texture.h
            render/gl/Mesh.h
            render/RenderView.h
            render/Viewport.h
            render/Material.h
            render/PbrMaterial.h
            render/RenderSystem.h
            scene/Camera.h
            scene/CameraSystem.h
            scene/Light.h
            scene/LightSystem.h
            render/Lighting.h
            render/Image.h
            render/ImageLoader.h
            render/Model.h
            render/ModelLoader.h
            util/Log.h)

target_include_directories(rabbet PUBLIC
    "${PROJECT_SOURCE_DIR}")

target_link_libraries(rabbet
    PUBLIC
        glad
        glfw
        glm::glm
        OpenGL::GL
    PRIVATE
        stb
        assimp::assimp)

target_compile_features(rabbet PUBLIC cxx_std_20)

rb_target_warnings(rabbet)

set(vulkan_triangle_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv")
set(vulkan_triangle_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv")
set(vulkan_objects_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/objects.vert.spv")
set(vulkan_objects_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/objects.frag.spv")
set(vulkan_flat_450_vert "${CMAKE_CURRENT_BINARY_DIR}/450/flat.vert")
set(vulkan_flat_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/flat.frag")
set(vulkan_flat_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/flat.vert.spv")
set(vulkan_flat_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/flat.frag.spv")
set(vulkan_triangle_cache "${CMAKE_CURRENT_BINARY_DIR}/pipeline.cache")

add_custom_command(
    OUTPUT "${vulkan_triangle_vertex_spv}"
    COMMAND Vulkan::glslc --target-env=vulkan1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/triangle.vert"
        -o "${vulkan_triangle_vertex_spv}"
    DEPENDS triangle.vert
    VERBATIM)

add_custom_command(
    OUTPUT "${vulkan_triangle_fragment_spv}"
    COMMAND Vulkan::glslc --target-env=vulkan1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/triangle.frag"
        -o "${vulkan_triangle_fragment_spv}"
    DEPENDS triangle.frag
    VERBATIM)

add_custom_command(
    OUTPUT "${vulkan_objects_vertex_spv}"
    COMMAND Vulkan::glslc --target-env=vulkan1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/objects.vert"
        -o "${vulkan_objects_vertex_spv}"
    DEPENDS objects.vert
    VERBATIM)

add_custom_command(
    OUTPUT "${vulkan_objects_fragment_spv}"
    COMMAND Vulkan::glslc --target-env=vulkan1.3
        "${CMAKE_CURRENT_SOURCE_DIR}/objects.frag"
        -o "${vulkan_objects_fragment_spv}"
    DEPENDS objects.frag
    VERBATIM)

# The flat pair lives with the engine shaders and carries the dialect macros, so its
# 450 text comes out of the same composer that builds the GL header, then glslc
# compiles it like the probe's own shaders.
add_custom_command(
    OUTPUT "${vulkan_flat_450_vert}"
    COMMAND ${CMAKE_COMMAND}
        "-DSHADER_DIR=${PROJECT_SOURCE_DIR}/rabbet/render/shaders"
        "-DOUTPUT=${vulkan_flat_450_vert}"
        "-DDIALECT=450"
        "-DPRELUDE=kFlatVertex"
        "-DMANIFEST=kFlatVertex=flat.vert"
        "-DDEPFILE=${vulkan_flat_450_vert}.d"
        -P "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/flat.vert"
        "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
        "${CMAKE_CURRENT_LIST_FILE}"
    DEPFILE "${vulkan_flat_450_vert}.d"
    COMMENT "Composing the 450 flat vertex shader"
    VERBATIM)

add_custom_command(
    OUTPUT "${vulkan_flat_450_frag}"
    COMMAND ${CMAKE_COMMAND}
        "-DSHADER_DIR=${PROJECT_SOURCE_DIR}/rabbet/render/shaders"
        "-DOUTPUT=${vulkan_flat_450_frag}"
        "-DDIALECT=450"
        "-DPRELUDE=kFlatFragment"
        "-DMANIFEST=kFlatFragment=flat.frag"
        "-DDEPFILE=${vulkan_flat_450_frag}.d"
        -P "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/flat.frag"
        "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
        "${CMAKE_CURRENT_LIST_FILE}"
    DEPFILE "${vulkan_flat_450_frag}.d"
    COMMENT "Composing the 450 flat fragment shader"
    VERBATIM)

add_custom_command(
    OUTPUT "${vulkan_flat_vertex_spv}"
    COMMAND Vulkan::glslc --target-env=vulkan1.3
        "${vulkan_flat_450_vert}"
        -o "${vulkan_flat_vertex_spv}"
    DEPENDS "${vulkan_flat_450_vert}"
    VERBATIM)

add_custom_command(
    OUTPUT "${vulkan_flat_fragment_spv}"
    COMMAND Vulkan::glslc --target-env=vulkan1.3
        "${vulkan_flat_450_frag}"
        -o "${vulkan_flat_fragment_spv}"
    DEPENDS "${vulkan_flat_450_frag}"
    VERBATIM)

add_executable(vulkan_triangle
    main.cpp
    ObjectsPass.cpp
    MeshDepthPass.cpp
    "${PROJECT_SOURCE_DIR}/rabbet/render/Geometry.cpp"
    "${vulkan_triangle_vertex_spv}"
    "${vulkan_triangle_fragment_spv}"
    "${vulkan_objects_vertex_spv}"
    "${vulkan_objects_fragment_spv}"
    "${vulkan_flat_vertex_spv}"
    "${vulkan_flat_fragment_spv}")

target_include_directories(vulkan_triangle PRIVATE
    "${PROJECT_SOURCE_DIR}")

target_link_libraries(vulkan_triangle PRIVATE
    Rabbet::Vulkan
    glm::glm)

target_compile_definitions(vulkan_triangle PRIVATE
    "RB_VULKAN_TRIANGLE_VERTEX_SPV=\"${vulkan_triangle_vertex_spv}\""
    "RB_VULKAN_TRIANGLE_FRAGMENT_SPV=\"${vulkan_triangle_fragment_spv}\""
    "RB_VULKAN_OBJECTS_VERTEX_SPV=\"${vulkan_objects_vertex_spv}\""
    "RB_VULKAN_OBJECTS_FRAGMENT_SPV=\"${vulkan_objects_fragment_spv}\""
    "RB_VULKAN_FLAT_VERTEX_SPV=\"${vulkan_flat_vertex_spv}\""
    "RB_VULKAN_FLAT_FRAGMENT_SPV=\"${vulkan_flat_fragment_spv}\""
    "RB_VULKAN_TRIANGLE_CACHE=\"${vulkan_triangle_cache}\"")

rb_target_warnings(vulkan_triangle)

set_target_properties(vulkan_triangle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

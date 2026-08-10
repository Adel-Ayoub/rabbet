set(vulkan_triangle_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv")
set(vulkan_triangle_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv")
set(vulkan_objects_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/objects.vert.spv")
set(vulkan_objects_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/objects.frag.spv")
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

add_executable(vulkan_triangle
    main.cpp
    ObjectsPass.cpp
    "${vulkan_triangle_vertex_spv}"
    "${vulkan_triangle_fragment_spv}"
    "${vulkan_objects_vertex_spv}"
    "${vulkan_objects_fragment_spv}")

target_include_directories(vulkan_triangle PRIVATE
    "${PROJECT_SOURCE_DIR}")

target_link_libraries(vulkan_triangle PRIVATE
    Rabbet::Vulkan)

target_compile_definitions(vulkan_triangle PRIVATE
    "RB_VULKAN_TRIANGLE_VERTEX_SPV=\"${vulkan_triangle_vertex_spv}\""
    "RB_VULKAN_TRIANGLE_FRAGMENT_SPV=\"${vulkan_triangle_fragment_spv}\""
    "RB_VULKAN_OBJECTS_VERTEX_SPV=\"${vulkan_objects_vertex_spv}\""
    "RB_VULKAN_OBJECTS_FRAGMENT_SPV=\"${vulkan_objects_fragment_spv}\""
    "RB_VULKAN_TRIANGLE_CACHE=\"${vulkan_triangle_cache}\"")

rb_target_warnings(vulkan_triangle)

set_target_properties(vulkan_triangle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

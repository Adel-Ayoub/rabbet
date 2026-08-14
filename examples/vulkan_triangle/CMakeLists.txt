set(vulkan_triangle_cache "${CMAKE_CURRENT_BINARY_DIR}/pipeline.cache")

add_executable(vulkan_triangle
    main.cpp
    ObjectsPass.cpp
    MeshDepthPass.cpp
    MaterialPass.cpp
    "${PROJECT_SOURCE_DIR}/rabbet/render/Geometry.cpp")

rb_target_vulkan_shader(vulkan_triangle
    RB_VULKAN_TRIANGLE_VERTEX_SPV triangle.vert)
rb_target_vulkan_shader(vulkan_triangle
    RB_VULKAN_TRIANGLE_FRAGMENT_SPV triangle.frag)
rb_target_vulkan_shader(vulkan_triangle
    RB_VULKAN_OBJECTS_VERTEX_SPV objects.vert)
rb_target_vulkan_shader(vulkan_triangle
    RB_VULKAN_OBJECTS_FRAGMENT_SPV objects.frag)

rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_FLAT_VERTEX_SPV kFlatVertex flat.vert)
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_FLAT_FRAGMENT_SPV kFlatFragment flat.frag)
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_DEPTH_VERTEX_SPV kDepthVertex depth.vert)
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_DEPTH_FRAGMENT_SPV kDepthFragment depth.frag NO_PRELUDE)
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_PICK_VERTEX_SPV kPickVertex pick.vert)
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_PICK_FRAGMENT_SPV kPickFragment pick.frag)

set(vulkan_material_dependencies
    light_uniforms.glsl
    shadow_functions.glsl
    pbr_functions.glsl)
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_LIT_VERTEX_SPV kLitVertex lit.vert
    DEPENDS ${vulkan_material_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_PBR_FRAGMENT_SPV kPbrFragment pbr.frag
    DEPENDS ${vulkan_material_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_PHONG_FRAGMENT_SPV kPhongFragment phong.frag
    DEPENDS ${vulkan_material_dependencies})

set(vulkan_hdr_dependencies light_uniforms.glsl)
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_SKYBOX_VERTEX_SPV kSkyboxVertex skybox.vert
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_SKYBOX_FRAGMENT_SPV kSkyboxFragment skybox.frag
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_CONVOLVE_VERTEX_SPV kConvolveVertex convolve.vert
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_CONVOLVE_FRAGMENT_SPV kConvolveFragment convolve.frag
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_FULLSCREEN_VERTEX_SPV kFullscreenVertex fullscreen.vert
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_PREFILTER_FRAGMENT_SPV kPrefilterFragment prefilter.frag
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_DOWNSAMPLE_FRAGMENT_SPV kDownsampleFragment downsample.frag
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_UPSAMPLE_FRAGMENT_SPV kUpsampleFragment upsample.frag
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_COMPOSITE_FRAGMENT_SPV kCompositeFragment composite.frag
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_FXAA_FRAGMENT_SPV kFxaaFragment fxaa.frag
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_WATER_VERTEX_SPV kWaterVertex water.vert
    DEPENDS ${vulkan_hdr_dependencies})
rb_target_composed_vulkan_shader(vulkan_triangle
    RB_VULKAN_WATER_FRAGMENT_SPV kWaterFragment water.frag
    DEPENDS ${vulkan_hdr_dependencies})

target_include_directories(vulkan_triangle PRIVATE
    "${PROJECT_SOURCE_DIR}")

target_link_libraries(vulkan_triangle PRIVATE
    Rabbet::Vulkan
    glm::glm)

target_compile_definitions(vulkan_triangle PRIVATE
    "RB_VULKAN_TRIANGLE_CACHE=\"${vulkan_triangle_cache}\"")

rb_target_warnings(vulkan_triangle)

set_target_properties(vulkan_triangle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

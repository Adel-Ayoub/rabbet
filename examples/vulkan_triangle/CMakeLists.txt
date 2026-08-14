set(vulkan_triangle_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/triangle.vert.spv")
set(vulkan_triangle_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/triangle.frag.spv")
set(vulkan_objects_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/objects.vert.spv")
set(vulkan_objects_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/objects.frag.spv")
set(vulkan_flat_450_vert "${CMAKE_CURRENT_BINARY_DIR}/450/flat.vert")
set(vulkan_flat_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/flat.frag")
set(vulkan_flat_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/flat.vert.spv")
set(vulkan_flat_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/flat.frag.spv")
set(vulkan_depth_450_vert "${CMAKE_CURRENT_BINARY_DIR}/450/depth.vert")
set(vulkan_depth_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/depth.frag")
set(vulkan_depth_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/depth.vert.spv")
set(vulkan_depth_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/depth.frag.spv")
set(vulkan_pick_450_vert "${CMAKE_CURRENT_BINARY_DIR}/450/pick.vert")
set(vulkan_pick_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/pick.frag")
set(vulkan_pick_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/pick.vert.spv")
set(vulkan_pick_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/pick.frag.spv")
set(vulkan_lit_450_vert "${CMAKE_CURRENT_BINARY_DIR}/450/lit.vert")
set(vulkan_pbr_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/pbr.frag")
set(vulkan_phong_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/phong.frag")
set(vulkan_lit_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/lit.vert.spv")
set(vulkan_pbr_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/pbr.frag.spv")
set(vulkan_phong_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/phong.frag.spv")
set(vulkan_skybox_450_vert "${CMAKE_CURRENT_BINARY_DIR}/450/skybox.vert")
set(vulkan_skybox_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/skybox.frag")
set(vulkan_skybox_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/skybox.vert.spv")
set(vulkan_skybox_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/skybox.frag.spv")
set(vulkan_convolve_450_vert "${CMAKE_CURRENT_BINARY_DIR}/450/convolve.vert")
set(vulkan_convolve_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/convolve.frag")
set(vulkan_convolve_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/convolve.vert.spv")
set(vulkan_convolve_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/convolve.frag.spv")
set(vulkan_fullscreen_450_vert "${CMAKE_CURRENT_BINARY_DIR}/450/fullscreen.vert")
set(vulkan_fullscreen_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/fullscreen.vert.spv")
set(vulkan_prefilter_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/prefilter.frag")
set(vulkan_downsample_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/downsample.frag")
set(vulkan_upsample_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/upsample.frag")
set(vulkan_composite_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/composite.frag")
set(vulkan_fxaa_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/fxaa.frag")
set(vulkan_prefilter_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/prefilter.frag.spv")
set(vulkan_downsample_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/downsample.frag.spv")
set(vulkan_upsample_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/upsample.frag.spv")
set(vulkan_composite_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/composite.frag.spv")
set(vulkan_fxaa_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/fxaa.frag.spv")
set(vulkan_water_450_vert "${CMAKE_CURRENT_BINARY_DIR}/450/water.vert")
set(vulkan_water_450_frag "${CMAKE_CURRENT_BINARY_DIR}/450/water.frag")
set(vulkan_water_vertex_spv "${CMAKE_CURRENT_BINARY_DIR}/water.vert.spv")
set(vulkan_water_fragment_spv "${CMAKE_CURRENT_BINARY_DIR}/water.frag.spv")
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

add_custom_command(
    OUTPUT "${vulkan_depth_450_vert}"
    COMMAND ${CMAKE_COMMAND}
        "-DSHADER_DIR=${PROJECT_SOURCE_DIR}/rabbet/render/shaders"
        "-DOUTPUT=${vulkan_depth_450_vert}"
        "-DDIALECT=450"
        "-DPRELUDE=kDepthVertex"
        "-DMANIFEST=kDepthVertex=depth.vert"
        "-DDEPFILE=${vulkan_depth_450_vert}.d"
        -P "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/depth.vert"
        "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
        "${CMAKE_CURRENT_LIST_FILE}"
    DEPFILE "${vulkan_depth_450_vert}.d"
    VERBATIM)

add_custom_command(
    OUTPUT "${vulkan_depth_450_frag}"
    COMMAND ${CMAKE_COMMAND}
        "-DSHADER_DIR=${PROJECT_SOURCE_DIR}/rabbet/render/shaders"
        "-DOUTPUT=${vulkan_depth_450_frag}"
        "-DDIALECT=450"
        "-DMANIFEST=kDepthFragment=depth.frag"
        "-DDEPFILE=${vulkan_depth_450_frag}.d"
        -P "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/depth.frag"
        "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
        "${CMAKE_CURRENT_LIST_FILE}"
    DEPFILE "${vulkan_depth_450_frag}.d"
    VERBATIM)

add_custom_command(
    OUTPUT "${vulkan_pick_450_vert}"
    COMMAND ${CMAKE_COMMAND}
        "-DSHADER_DIR=${PROJECT_SOURCE_DIR}/rabbet/render/shaders"
        "-DOUTPUT=${vulkan_pick_450_vert}"
        "-DDIALECT=450"
        "-DPRELUDE=kPickVertex"
        "-DMANIFEST=kPickVertex=pick.vert"
        "-DDEPFILE=${vulkan_pick_450_vert}.d"
        -P "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/pick.vert"
        "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
        "${CMAKE_CURRENT_LIST_FILE}"
    DEPFILE "${vulkan_pick_450_vert}.d"
    VERBATIM)

add_custom_command(
    OUTPUT "${vulkan_pick_450_frag}"
    COMMAND ${CMAKE_COMMAND}
        "-DSHADER_DIR=${PROJECT_SOURCE_DIR}/rabbet/render/shaders"
        "-DOUTPUT=${vulkan_pick_450_frag}"
        "-DDIALECT=450"
        "-DPRELUDE=kPickFragment"
        "-DMANIFEST=kPickFragment=pick.frag"
        "-DDEPFILE=${vulkan_pick_450_frag}.d"
        -P "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
    DEPENDS
        "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/pick.frag"
        "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
        "${CMAKE_CURRENT_LIST_FILE}"
    DEPFILE "${vulkan_pick_450_frag}.d"
    VERBATIM)

foreach(vulkan_offscreen_stage IN ITEMS depth_vertex depth_fragment pick_vertex pick_fragment)
    if(vulkan_offscreen_stage STREQUAL "depth_vertex")
        set(vulkan_offscreen_source "${vulkan_depth_450_vert}")
        set(vulkan_offscreen_spv "${vulkan_depth_vertex_spv}")
    elseif(vulkan_offscreen_stage STREQUAL "depth_fragment")
        set(vulkan_offscreen_source "${vulkan_depth_450_frag}")
        set(vulkan_offscreen_spv "${vulkan_depth_fragment_spv}")
    elseif(vulkan_offscreen_stage STREQUAL "pick_vertex")
        set(vulkan_offscreen_source "${vulkan_pick_450_vert}")
        set(vulkan_offscreen_spv "${vulkan_pick_vertex_spv}")
    else()
        set(vulkan_offscreen_source "${vulkan_pick_450_frag}")
        set(vulkan_offscreen_spv "${vulkan_pick_fragment_spv}")
    endif()
    add_custom_command(
        OUTPUT "${vulkan_offscreen_spv}"
        COMMAND Vulkan::glslc --target-env=vulkan1.3
            "${vulkan_offscreen_source}"
            -o "${vulkan_offscreen_spv}"
        DEPENDS "${vulkan_offscreen_source}"
        VERBATIM)
endforeach()

foreach(vulkan_material_shader IN ITEMS lit pbr phong)
    if(vulkan_material_shader STREQUAL "lit")
        set(vulkan_material_constant kLitVertex)
        set(vulkan_material_source lit.vert)
        set(vulkan_material_output "${vulkan_lit_450_vert}")
        set(vulkan_material_spv "${vulkan_lit_vertex_spv}")
    elseif(vulkan_material_shader STREQUAL "pbr")
        set(vulkan_material_constant kPbrFragment)
        set(vulkan_material_source pbr.frag)
        set(vulkan_material_output "${vulkan_pbr_450_frag}")
        set(vulkan_material_spv "${vulkan_pbr_fragment_spv}")
    else()
        set(vulkan_material_constant kPhongFragment)
        set(vulkan_material_source phong.frag)
        set(vulkan_material_output "${vulkan_phong_450_frag}")
        set(vulkan_material_spv "${vulkan_phong_fragment_spv}")
    endif()
    add_custom_command(
        OUTPUT "${vulkan_material_output}"
        COMMAND ${CMAKE_COMMAND}
            "-DSHADER_DIR=${PROJECT_SOURCE_DIR}/rabbet/render/shaders"
            "-DOUTPUT=${vulkan_material_output}"
            "-DDIALECT=450"
            "-DPRELUDE=${vulkan_material_constant}"
            "-DMANIFEST=${vulkan_material_constant}=${vulkan_material_source}"
            "-DDEPFILE=${vulkan_material_output}.d"
            -P "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
        DEPENDS
            "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/${vulkan_material_source}"
            "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/light_uniforms.glsl"
            "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/shadow_functions.glsl"
            "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/pbr_functions.glsl"
            "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
            "${CMAKE_CURRENT_LIST_FILE}"
        DEPFILE "${vulkan_material_output}.d"
        COMMENT "Composing the 450 ${vulkan_material_source} shader"
        VERBATIM)
    add_custom_command(
        OUTPUT "${vulkan_material_spv}"
        COMMAND Vulkan::glslc --target-env=vulkan1.3
            "${vulkan_material_output}"
            -o "${vulkan_material_spv}"
        DEPENDS "${vulkan_material_output}"
        VERBATIM)
endforeach()

function(rb_add_vulkan_hdr_shader constant source composed spv)
    add_custom_command(
        OUTPUT "${composed}"
        COMMAND ${CMAKE_COMMAND}
            "-DSHADER_DIR=${PROJECT_SOURCE_DIR}/rabbet/render/shaders"
            "-DOUTPUT=${composed}"
            "-DDIALECT=450"
            "-DPRELUDE=${constant}"
            "-DMANIFEST=${constant}=${source}"
            "-DDEPFILE=${composed}.d"
            -P "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
        DEPENDS
            "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/${source}"
            "${PROJECT_SOURCE_DIR}/rabbet/render/shaders/light_uniforms.glsl"
            "${PROJECT_SOURCE_DIR}/cmake/RabbetShaderCompose.cmake"
            "${CMAKE_CURRENT_LIST_FILE}"
        DEPFILE "${composed}.d"
        COMMENT "Composing the 450 ${source} shader"
        VERBATIM)
    add_custom_command(
        OUTPUT "${spv}"
        COMMAND Vulkan::glslc --target-env=vulkan1.3 "${composed}" -o "${spv}"
        DEPENDS "${composed}"
        VERBATIM)
endfunction()

rb_add_vulkan_hdr_shader(kSkyboxVertex skybox.vert
    "${vulkan_skybox_450_vert}" "${vulkan_skybox_vertex_spv}")
rb_add_vulkan_hdr_shader(kSkyboxFragment skybox.frag
    "${vulkan_skybox_450_frag}" "${vulkan_skybox_fragment_spv}")
rb_add_vulkan_hdr_shader(kConvolveVertex convolve.vert
    "${vulkan_convolve_450_vert}" "${vulkan_convolve_vertex_spv}")
rb_add_vulkan_hdr_shader(kConvolveFragment convolve.frag
    "${vulkan_convolve_450_frag}" "${vulkan_convolve_fragment_spv}")
rb_add_vulkan_hdr_shader(kFullscreenVertex fullscreen.vert
    "${vulkan_fullscreen_450_vert}" "${vulkan_fullscreen_vertex_spv}")
rb_add_vulkan_hdr_shader(kPrefilterFragment prefilter.frag
    "${vulkan_prefilter_450_frag}" "${vulkan_prefilter_fragment_spv}")
rb_add_vulkan_hdr_shader(kDownsampleFragment downsample.frag
    "${vulkan_downsample_450_frag}" "${vulkan_downsample_fragment_spv}")
rb_add_vulkan_hdr_shader(kUpsampleFragment upsample.frag
    "${vulkan_upsample_450_frag}" "${vulkan_upsample_fragment_spv}")
rb_add_vulkan_hdr_shader(kCompositeFragment composite.frag
    "${vulkan_composite_450_frag}" "${vulkan_composite_fragment_spv}")
rb_add_vulkan_hdr_shader(kFxaaFragment fxaa.frag
    "${vulkan_fxaa_450_frag}" "${vulkan_fxaa_fragment_spv}")
rb_add_vulkan_hdr_shader(kWaterVertex water.vert
    "${vulkan_water_450_vert}" "${vulkan_water_vertex_spv}")
rb_add_vulkan_hdr_shader(kWaterFragment water.frag
    "${vulkan_water_450_frag}" "${vulkan_water_fragment_spv}")

add_executable(vulkan_triangle
    main.cpp
    ObjectsPass.cpp
    MeshDepthPass.cpp
    MaterialPass.cpp
    "${PROJECT_SOURCE_DIR}/rabbet/render/Geometry.cpp"
    "${vulkan_triangle_vertex_spv}"
    "${vulkan_triangle_fragment_spv}"
    "${vulkan_objects_vertex_spv}"
    "${vulkan_objects_fragment_spv}"
    "${vulkan_flat_vertex_spv}"
    "${vulkan_flat_fragment_spv}"
    "${vulkan_depth_vertex_spv}"
    "${vulkan_depth_fragment_spv}"
    "${vulkan_pick_vertex_spv}"
    "${vulkan_pick_fragment_spv}"
    "${vulkan_lit_vertex_spv}"
    "${vulkan_pbr_fragment_spv}"
    "${vulkan_phong_fragment_spv}"
    "${vulkan_skybox_vertex_spv}"
    "${vulkan_skybox_fragment_spv}"
    "${vulkan_convolve_vertex_spv}"
    "${vulkan_convolve_fragment_spv}"
    "${vulkan_fullscreen_vertex_spv}"
    "${vulkan_prefilter_fragment_spv}"
    "${vulkan_downsample_fragment_spv}"
    "${vulkan_upsample_fragment_spv}"
    "${vulkan_composite_fragment_spv}"
    "${vulkan_fxaa_fragment_spv}"
    "${vulkan_water_vertex_spv}"
    "${vulkan_water_fragment_spv}")

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
    "RB_VULKAN_DEPTH_VERTEX_SPV=\"${vulkan_depth_vertex_spv}\""
    "RB_VULKAN_DEPTH_FRAGMENT_SPV=\"${vulkan_depth_fragment_spv}\""
    "RB_VULKAN_PICK_VERTEX_SPV=\"${vulkan_pick_vertex_spv}\""
    "RB_VULKAN_PICK_FRAGMENT_SPV=\"${vulkan_pick_fragment_spv}\""
    "RB_VULKAN_LIT_VERTEX_SPV=\"${vulkan_lit_vertex_spv}\""
    "RB_VULKAN_PBR_FRAGMENT_SPV=\"${vulkan_pbr_fragment_spv}\""
    "RB_VULKAN_PHONG_FRAGMENT_SPV=\"${vulkan_phong_fragment_spv}\""
    "RB_VULKAN_SKYBOX_VERTEX_SPV=\"${vulkan_skybox_vertex_spv}\""
    "RB_VULKAN_SKYBOX_FRAGMENT_SPV=\"${vulkan_skybox_fragment_spv}\""
    "RB_VULKAN_CONVOLVE_VERTEX_SPV=\"${vulkan_convolve_vertex_spv}\""
    "RB_VULKAN_CONVOLVE_FRAGMENT_SPV=\"${vulkan_convolve_fragment_spv}\""
    "RB_VULKAN_FULLSCREEN_VERTEX_SPV=\"${vulkan_fullscreen_vertex_spv}\""
    "RB_VULKAN_PREFILTER_FRAGMENT_SPV=\"${vulkan_prefilter_fragment_spv}\""
    "RB_VULKAN_DOWNSAMPLE_FRAGMENT_SPV=\"${vulkan_downsample_fragment_spv}\""
    "RB_VULKAN_UPSAMPLE_FRAGMENT_SPV=\"${vulkan_upsample_fragment_spv}\""
    "RB_VULKAN_COMPOSITE_FRAGMENT_SPV=\"${vulkan_composite_fragment_spv}\""
    "RB_VULKAN_FXAA_FRAGMENT_SPV=\"${vulkan_fxaa_fragment_spv}\""
    "RB_VULKAN_WATER_VERTEX_SPV=\"${vulkan_water_vertex_spv}\""
    "RB_VULKAN_WATER_FRAGMENT_SPV=\"${vulkan_water_fragment_spv}\""
    "RB_VULKAN_TRIANGLE_CACHE=\"${vulkan_triangle_cache}\"")

rb_target_warnings(vulkan_triangle)

set_target_properties(vulkan_triangle PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

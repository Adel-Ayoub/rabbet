set(RABBET_TEST_SUITES
    ecs_entity
    ecs_pool
    ecs_scene
    ecs_recycle
    ecs_query
    ecs_stress
    ecs_pool_invariants
    core_resource
    core_runtime
    core_module
    core_clock
    core_scheduler
    script
    physics
    audio
    material
    util_log
    scene_transform
    scene_camera
    scene_light
    name_index
    lighting_extended
    particle
    terrain
    tonemap
    camera_view
    hierarchy
    render_geometry
    render_shadow
    render_sky
    render_image_loader
    render_model_loader
    serialize_scene
    serialize_duplicate
    serialize_registry
    prefab
    assets_uuid
    assets_manager
    assets_meta
    assets_database
    asset_tree
    thumbnail_cache
    assets_render
    assets_scene_file
    assets_resolve
    asset_create
    render_water
    orbhunt
    lanternwaves)

foreach(suite IN LISTS RABBET_TEST_SUITES)
    add_executable(${suite}_tests ${suite}_tests.cpp)
    target_link_libraries(${suite}_tests PRIVATE Rabbet::Rabbet)
    rb_target_warnings(${suite}_tests)
    set_target_properties(${suite}_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_test(NAME ${suite} COMMAND ${suite}_tests)
    # A deadlock in an embedded runtime (Jolt, Lua, miniaudio) should fail the suite,
    # not wedge ctest; the slowest suite finishes in ~2s, so 120 is generous.
    set_tests_properties(${suite} PROPERTIES TIMEOUT 120)
endforeach()

# The audio suite decodes a committed Ogg Vorbis fixture to verify the stb_vorbis import path.
target_compile_definitions(audio_tests PRIVATE
    RB_AUDIO_TEST_OGG="${PROJECT_SOURCE_DIR}/examples/forge/assets/audio/chime.ogg")

# The orbhunt suite drives the committed demo content (scene + prefabs + Lua) end to end.
target_compile_definitions(orbhunt_tests PRIVATE
    RB_FORGE_ASSETS="${PROJECT_SOURCE_DIR}/examples/forge/assets")

# The prefab suite instantiates the committed sample nested prefab (the lamp post).
target_compile_definitions(prefab_tests PRIVATE
    RB_FORGE_ASSETS="${PROJECT_SOURCE_DIR}/examples/forge/assets")

# The lantern waves suite plays the second demo (title + arena scenes, spawned wisps) headlessly.
target_compile_definitions(lanternwaves_tests PRIVATE
    RB_FORGE_ASSETS="${PROJECT_SOURCE_DIR}/examples/forge/assets")

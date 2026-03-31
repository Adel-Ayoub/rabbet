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
    util_log
    scene_transform
    scene_camera
    scene_light
    render_geometry
    render_shadow
    render_sky
    render_image_loader
    render_model_loader
    serialize_scene
    serialize_duplicate
    assets_uuid
    assets_manager
    assets_meta
    assets_database
    assets_render
    assets_scene_file
    assets_resolve)

foreach(suite IN LISTS RABBET_TEST_SUITES)
    add_executable(${suite}_tests ${suite}_tests.cpp)
    target_link_libraries(${suite}_tests PRIVATE Rabbet::Rabbet)
    rb_target_warnings(${suite}_tests)
    set_target_properties(${suite}_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_test(NAME ${suite} COMMAND ${suite}_tests)
endforeach()

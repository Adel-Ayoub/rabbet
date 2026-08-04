# One test file and one executable per subsystem. A deadlock in an embedded runtime (Jolt,
# Lua, miniaudio) should fail the suite, not wedge ctest; the slowest suite finishes in a
# few seconds, so 120 is generous.
function(rb_test_suite name)
    add_executable(${name}_tests ${ARGN})
    target_link_libraries(${name}_tests PRIVATE Rabbet::Rabbet)
    rb_target_warnings(${name}_tests)
    set_target_properties(${name}_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_test(NAME ${name} COMMAND ${name}_tests)
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

rb_test_suite(core core_tests.cpp)
rb_test_suite(ecs ecs_tests.cpp)
rb_test_suite(scene scene_tests.cpp)
rb_test_suite(assets assets_tests.cpp)
rb_test_suite(render render_tests.cpp)
rb_test_suite(serialize serialize_tests.cpp)
rb_test_suite(content content_tests.cpp)
rb_test_suite(particle particle_tests.cpp)
rb_test_suite(script script_tests.cpp)
rb_test_suite(physics physics_tests.cpp)
rb_test_suite(audio audio_tests.cpp)
rb_test_suite(terrain terrain_tests.cpp)

# The audio suite decodes a committed Ogg Vorbis fixture to verify the stb_vorbis import path.
target_compile_definitions(audio_tests PRIVATE
    RB_AUDIO_TEST_OGG="${PROJECT_SOURCE_DIR}/examples/forge/assets/audio/chime.ogg")

# The serialize suite instantiates the committed sample nested prefab (the lamp post), and the
# content suite plays both shipped demos (Orb Hunt and Lantern Waves) headlessly.
target_compile_definitions(serialize_tests PRIVATE
    RB_FORGE_ASSETS="${PROJECT_SOURCE_DIR}/examples/forge/assets")
target_compile_definitions(content_tests PRIVATE
    RB_FORGE_ASSETS="${PROJECT_SOURCE_DIR}/examples/forge/assets")

foreach(suite IN ITEMS entity pool scene)
    add_executable(ecs_${suite}_tests ecs_${suite}_tests.cpp)
    target_link_libraries(ecs_${suite}_tests PRIVATE Rabbet::Rabbet)
    rb_target_warnings(ecs_${suite}_tests)
    set_target_properties(ecs_${suite}_tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")
    add_test(NAME ecs_${suite} COMMAND ecs_${suite}_tests)
endforeach()

add_executable(sandbox
    main.cpp)

target_link_libraries(sandbox PRIVATE
    Rabbet::Rabbet)

target_compile_definitions(sandbox PRIVATE
    "RB_SANDBOX_ASSETS=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\"")

rb_target_warnings(sandbox)

set_target_properties(sandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

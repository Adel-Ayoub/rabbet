add_executable(atrium
    main.cpp)

target_link_libraries(atrium PRIVATE
    example_common
    Rabbet::Rabbet)

target_compile_definitions(atrium PRIVATE
    "RB_ATRIUM_ASSETS=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\"")

rb_target_warnings(atrium)

set_target_properties(atrium PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

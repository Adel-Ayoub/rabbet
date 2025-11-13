add_executable(garden
    main.cpp)

target_link_libraries(garden PRIVATE
    example_common
    Rabbet::Rabbet)

target_compile_definitions(garden PRIVATE
    "RB_GARDEN_ASSETS=\"${CMAKE_CURRENT_SOURCE_DIR}/assets\"")

rb_target_warnings(garden)

set_target_properties(garden PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

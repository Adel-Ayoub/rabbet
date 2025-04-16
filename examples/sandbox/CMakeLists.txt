add_executable(sandbox
    main.cpp)

target_link_libraries(sandbox PRIVATE
    Rabbet::Rabbet)

rb_target_warnings(sandbox)

set_target_properties(sandbox PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

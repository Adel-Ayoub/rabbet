# GLAD loader, vendored as its own target so first-party warning flags
# never apply to this generated third-party code.

add_library(glad STATIC
    glad/src/glad.c)

target_include_directories(glad SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/glad/include")

set_target_properties(glad PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# stb_image: single-header image loader, with the implementation isolated in stb_image.cpp.
add_library(stb STATIC
    stb/stb_image.cpp)

target_include_directories(stb SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/stb")

set_target_properties(stb PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

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

# miniaudio: single-header audio playback library, with the implementation isolated in
# miniaudio.c. SYSTEM include so its header never trips first-party -Werror; linked
# PRIVATE to the rabbet core behind AudioSystem's pImpl, like stb/sol2/Jolt.
add_library(miniaudio STATIC
    miniaudio/miniaudio.c)

target_include_directories(miniaudio SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/miniaudio")

set_target_properties(miniaudio PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# CoreAudio/AudioToolbox on Apple; the implementation also calls into pthread/dl elsewhere.
if(APPLE)
    target_link_libraries(miniaudio PUBLIC
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework AudioToolbox")
elseif(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(miniaudio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

# stb_vorbis: single-file Ogg Vorbis decoder. Built as its own static lib so its many warnings
# never reach first-party -Werror; consumers include it with STB_VORBIS_HEADER_ONLY for the
# declarations and call stb_vorbis_decode_filename to decode a clip to PCM.
add_library(stb_vorbis STATIC
    stb_vorbis/stb_vorbis.c)

target_include_directories(stb_vorbis SYSTEM PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/stb_vorbis")

set_target_properties(stb_vorbis PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

if(UNIX AND NOT APPLE)
    target_link_libraries(stb_vorbis PUBLIC m)
endif()

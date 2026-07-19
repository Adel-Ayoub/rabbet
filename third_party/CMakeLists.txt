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

# nfd (Native File Dialog Extended): native open/save panels behind a small C API.
# One platform backend compiles per OS. SYSTEM include so its headers never trip
# first-party -Werror; linked PRIVATE to the editor only, the engine core never
# opens a dialog. Gated on the editor building at all, so an engine-only configure
# never requires the platform dialog deps (GTK3 on Linux).
if(RABBET_BUILD_EDITOR)
    if(APPLE)
        add_library(nfd STATIC
            nfd/nfd_cocoa.m)
        target_link_libraries(nfd PUBLIC "-framework AppKit")
        # UniformTypeIdentifiers backs the allowed-content-types filter API the cocoa
        # backend compiles in on a macOS 11+ floor; an older explicit target takes the
        # allowedFileTypes fallback and must not hard-link a framework its floor lacks.
        if(NOT CMAKE_OSX_DEPLOYMENT_TARGET OR CMAKE_OSX_DEPLOYMENT_TARGET VERSION_GREATER_EQUAL 11.0)
            target_link_libraries(nfd PUBLIC "-framework UniformTypeIdentifiers")
        endif()
    elseif(WIN32)
        add_library(nfd STATIC
            nfd/nfd_win.cpp)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
        add_library(nfd STATIC
            nfd/nfd_gtk.cpp)
        # PRIVATE keeps the gtk include dirs out of consumers; a static lib's link
        # dependencies still reach the final executable.
        target_link_libraries(nfd PRIVATE PkgConfig::GTK3)
    endif()

    target_include_directories(nfd SYSTEM PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/nfd")

    set_target_properties(nfd PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
endif()

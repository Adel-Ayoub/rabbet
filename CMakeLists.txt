cmake_minimum_required(VERSION 3.24)

project(Rabbet
    VERSION 0.1.0
    DESCRIPTION "A modern C++20 sparse-set ECS game engine"
    LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(RabbetWarnings)

enable_testing()

include(FetchContent)
FetchContent_Declare(
    glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG 3.4
    GIT_SHALLOW ON
    FIND_PACKAGE_ARGS NAMES glfw3)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG 1.0.1
    GIT_SHALLOW ON)
set(GLM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLM_BUILD_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glm)
if(TARGET glm)
    get_target_property(_glm_includes glm INTERFACE_INCLUDE_DIRECTORIES)
    if(_glm_includes)
        set_target_properties(glm PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_glm_includes}")
    endif()
endif()

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW ON
    FIND_PACKAGE_ARGS NAMES nlohmann_json)
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_MakeAvailable(nlohmann_json)
if(TARGET nlohmann_json)
    get_target_property(_nlohmann_includes nlohmann_json INTERFACE_INCLUDE_DIRECTORIES)
    if(_nlohmann_includes)
        set_target_properties(nlohmann_json PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_nlohmann_includes}")
    endif()
endif()

# Keep first-party and fetched dependencies static; assimp would otherwise default to shared.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    assimp
    GIT_REPOSITORY https://github.com/assimp/assimp.git
    GIT_TAG v5.4.3
    GIT_SHALLOW ON
    FIND_PACKAGE_ARGS NAMES assimp)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF CACHE BOOL "" FORCE)
set(ASSIMP_NO_EXPORT ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_OBJ_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_GLTF_IMPORTER ON CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_FBX_IMPORTER ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(assimp)
if(TARGET assimp)
    if(NOT TARGET assimp::assimp)
        add_library(assimp::assimp ALIAS assimp)
    endif()
    get_target_property(_assimp_includes assimp INTERFACE_INCLUDE_DIRECTORIES)
    if(_assimp_includes)
        set_target_properties(assimp PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_assimp_includes}")
    endif()
endif()

# Lua + sol2 for the scripting subsystem. Lua ships no CMake of its own, so fetch the
# sources and build a small static lib here (the list is Lua 5.4's library, without the
# lua.c/luac.c front-ends). sol2 is header-only; expose it as an INTERFACE target over
# its include tree. Both are SYSTEM includes so their headers never trip the strict
# -Werror warnings in first-party code that includes <sol/sol.hpp>.
FetchContent_Declare(
    lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG v5.4.7
    GIT_SHALLOW ON
    SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(lua)
add_library(lua STATIC
    "${lua_SOURCE_DIR}/lapi.c" "${lua_SOURCE_DIR}/lauxlib.c" "${lua_SOURCE_DIR}/lbaselib.c"
    "${lua_SOURCE_DIR}/lcode.c" "${lua_SOURCE_DIR}/lcorolib.c" "${lua_SOURCE_DIR}/lctype.c"
    "${lua_SOURCE_DIR}/ldblib.c" "${lua_SOURCE_DIR}/ldebug.c" "${lua_SOURCE_DIR}/ldo.c"
    "${lua_SOURCE_DIR}/ldump.c" "${lua_SOURCE_DIR}/lfunc.c" "${lua_SOURCE_DIR}/lgc.c"
    "${lua_SOURCE_DIR}/linit.c" "${lua_SOURCE_DIR}/liolib.c" "${lua_SOURCE_DIR}/llex.c"
    "${lua_SOURCE_DIR}/lmathlib.c" "${lua_SOURCE_DIR}/lmem.c" "${lua_SOURCE_DIR}/loadlib.c"
    "${lua_SOURCE_DIR}/lobject.c" "${lua_SOURCE_DIR}/lopcodes.c" "${lua_SOURCE_DIR}/loslib.c"
    "${lua_SOURCE_DIR}/lparser.c" "${lua_SOURCE_DIR}/lstate.c" "${lua_SOURCE_DIR}/lstring.c"
    "${lua_SOURCE_DIR}/lstrlib.c" "${lua_SOURCE_DIR}/ltable.c" "${lua_SOURCE_DIR}/ltablib.c"
    "${lua_SOURCE_DIR}/ltm.c" "${lua_SOURCE_DIR}/lundump.c" "${lua_SOURCE_DIR}/lutf8lib.c"
    "${lua_SOURCE_DIR}/lvm.c" "${lua_SOURCE_DIR}/lzio.c")
target_include_directories(lua SYSTEM PUBLIC "${lua_SOURCE_DIR}")
set_target_properties(lua PROPERTIES POSITION_INDEPENDENT_CODE ON)
if(NOT WIN32)
    target_compile_definitions(lua PRIVATE LUA_USE_POSIX)
endif()

FetchContent_Declare(
    sol2
    GIT_REPOSITORY https://github.com/ThePhD/sol2.git
    GIT_TAG v3.5.0
    GIT_SHALLOW ON
    SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(sol2)
add_library(sol2 INTERFACE)
target_include_directories(sol2 SYSTEM INTERFACE "${sol2_SOURCE_DIR}/include")
target_compile_definitions(sol2 INTERFACE SOL_NO_LUA_HPP=1 SOL_ALL_SAFETIES_ON=1)
target_link_libraries(sol2 INTERFACE lua)

find_package(OpenGL REQUIRED)

option(RABBET_BUILD_TESTS "Build the unit test suite" ON)

add_subdirectory(third_party)
add_subdirectory(rabbet)
add_subdirectory(examples)

if(RABBET_BUILD_TESTS)
    add_subdirectory(tests)
endif()

option(RABBET_BUILD_EDITOR "Build the ImGui editor" ON)
if(RABBET_BUILD_EDITOR)
    add_subdirectory(editor)
endif()

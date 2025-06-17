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

find_package(OpenGL REQUIRED)

option(RABBET_BUILD_TESTS "Build the unit test suite" ON)

add_subdirectory(third_party)
add_subdirectory(rabbet)
add_subdirectory(examples)

if(RABBET_BUILD_TESTS)
    add_subdirectory(tests)
endif()

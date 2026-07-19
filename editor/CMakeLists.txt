include(FetchContent)
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.91.9b-docking
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(imgui)

# ImGuizmo has no tagged releases; pin a specific commit. SOURCE_SUBDIR points at a
# non-existent dir so MakeAvailable only fetches it (we compile ImGuizmo.cpp into the
# imgui lib ourselves rather than running ImGuizmo's own CMake).
FetchContent_Declare(
    imguizmo
    GIT_REPOSITORY https://github.com/CedricGuillemet/ImGuizmo.git
    GIT_TAG be8aa4aeab86b402701c8c1df011bd8cd776760b
    SOURCE_SUBDIR do-not-build)
FetchContent_MakeAvailable(imguizmo)

add_library(imgui STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
    "${imguizmo_SOURCE_DIR}/src/ImGuizmo.cpp")
target_include_directories(imgui SYSTEM PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
    "${imguizmo_SOURCE_DIR}/src")
target_link_libraries(imgui PUBLIC glfw OpenGL::GL)
set_target_properties(imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Header-only reflection for enum<->name; editor-only (the engine core needs none).
FetchContent_Declare(
    magic_enum
    GIT_REPOSITORY https://github.com/Neargye/magic_enum.git
    GIT_TAG v0.9.7
    GIT_SHALLOW ON)
set(MAGIC_ENUM_OPT_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(MAGIC_ENUM_OPT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(MAGIC_ENUM_OPT_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(magic_enum)
if(TARGET magic_enum)
    get_target_property(_magic_enum_includes magic_enum INTERFACE_INCLUDE_DIRECTORIES)
    if(_magic_enum_includes)
        set_target_properties(magic_enum PROPERTIES
            INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_magic_enum_includes}")
    endif()
endif()

add_executable(forge
    main.cpp
    Editor.cpp
    Palette.cpp
    PanelManager.cpp
    LogBuffer.cpp
    EditorCamera.cpp
    ComponentDrawers.cpp
    ThumbnailRenderer.cpp
    AssetAssign.cpp
    Widgets.cpp
    panels/ConsolePanel.cpp
    panels/HierarchyPanel.cpp
    panels/ViewportPanel.cpp
    panels/InspectorPanel.cpp
    panels/AssetsPanel.cpp)
target_link_libraries(forge PRIVATE
    Rabbet::Rabbet
    imgui
    magic_enum::magic_enum
    nfd)
target_include_directories(forge PRIVATE "${PROJECT_SOURCE_DIR}")
target_compile_definitions(forge PRIVATE
    RB_EDITOR_RESOURCES="${CMAKE_CURRENT_SOURCE_DIR}/resources"
    RB_EDITOR_ASSETS="${PROJECT_SOURCE_DIR}/examples/forge/assets"
    RB_EDITOR_WORKSPACE="${CMAKE_CURRENT_SOURCE_DIR}/workspace")
rb_target_warnings(forge)
set_target_properties(forge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

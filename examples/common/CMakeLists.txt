add_library(example_common STATIC
    FlyCameraSystem.cpp
    SceneLoader.cpp)

target_link_libraries(example_common PUBLIC
    Rabbet::Rabbet)

rb_target_warnings(example_common)

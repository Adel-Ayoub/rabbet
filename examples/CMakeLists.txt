add_subdirectory(common)
add_subdirectory(sandbox)
add_subdirectory(atrium)
add_subdirectory(garden)

if(RABBET_VULKAN)
    add_subdirectory(vulkan_triangle)
endif()

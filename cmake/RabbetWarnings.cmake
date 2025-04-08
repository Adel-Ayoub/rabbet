# Strict, per-target warning flags for first-party Rabbet code.
# Third-party targets (glad, glfw, ...) are deliberately never passed through here.

option(RABBET_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

function(rb_target_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /Zc:__cplusplus
            /EHsc
            $<$<BOOL:${RABBET_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wconversion
            -Wsign-conversion
            -Wnon-virtual-dtor
            -Woverloaded-virtual
            -Wcast-align
            -Wunused
            -Wnull-dereference
            -Wdouble-promotion
            -Wformat=2
            $<$<BOOL:${RABBET_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()
endfunction()

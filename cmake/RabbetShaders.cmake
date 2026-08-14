include_guard(GLOBAL)

function(_rb_compile_vulkan_shader output_variable target definition source)
    if(IS_ABSOLUTE "${source}")
        set(source_path "${source}")
    else()
        set(source_path "${CMAKE_CURRENT_SOURCE_DIR}/${source}")
    endif()
    cmake_path(GET source_path FILENAME source_name)
    set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/${target}/${definition}")
    set(output "${output_dir}/${source_name}.spv")

    add_custom_command(
        OUTPUT "${output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
        COMMAND Vulkan::glslc --target-env=vulkan1.3 "${source_path}" -o "${output}"
        DEPENDS
            "${source_path}"
            "${CMAKE_CURRENT_LIST_FILE}"
            "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        VERBATIM)

    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()

function(_rb_compose_vulkan_shader output_variable target definition constant source)
    set(options NO_PRELUDE)
    set(one_value_args SHADER_DIR)
    set(multi_value_args DEPENDS)
    cmake_parse_arguments(PARSE_ARGV 5 shader
        "${options}" "${one_value_args}" "${multi_value_args}")
    if(shader_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "unknown Vulkan shader arguments ${shader_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT shader_SHADER_DIR)
        set(shader_SHADER_DIR "${PROJECT_SOURCE_DIR}/rabbet/render/shaders")
    endif()

    set(source_path "${shader_SHADER_DIR}/${source}")
    cmake_path(GET source_path FILENAME source_name)
    set(composed_dir "${CMAKE_CURRENT_BINARY_DIR}/shaders/${target}/${definition}/450")
    set(composed "${composed_dir}/${source_name}")
    set(prelude_argument)
    if(NOT shader_NO_PRELUDE)
        set(prelude_argument "-DPRELUDE=${constant}")
    endif()

    set(dependencies "${source_path}")
    foreach(dependency IN LISTS shader_DEPENDS)
        if(IS_ABSOLUTE "${dependency}")
            list(APPEND dependencies "${dependency}")
        else()
            list(APPEND dependencies "${shader_SHADER_DIR}/${dependency}")
        endif()
    endforeach()

    set(composer "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RabbetShaderCompose.cmake")
    add_custom_command(
        OUTPUT "${composed}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${composed_dir}"
        COMMAND ${CMAKE_COMMAND}
            "-DSHADER_DIR=${shader_SHADER_DIR}"
            "-DOUTPUT=${composed}"
            "-DDIALECT=450"
            ${prelude_argument}
            "-DMANIFEST=${constant}=${source}"
            "-DDEPFILE=${composed}.d"
            -P "${composer}"
        DEPENDS
            ${dependencies}
            "${composer}"
            "${CMAKE_CURRENT_LIST_FILE}"
            "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        DEPFILE "${composed}.d"
        COMMENT "Composing the 450 ${source} shader"
        VERBATIM)

    _rb_compile_vulkan_shader(output "${target}" "${definition}" "${composed}")
    set(${output_variable} "${output}" PARENT_SCOPE)
endfunction()

function(rb_target_vulkan_shader target definition source)
    _rb_compile_vulkan_shader(output "${target}" "${definition}" "${source}")
    target_sources(${target} PRIVATE "${output}")
    target_compile_definitions(${target} PRIVATE "${definition}=\"${output}\"")
endfunction()

function(rb_target_composed_vulkan_shader target definition constant source)
    _rb_compose_vulkan_shader(output "${target}" "${definition}" "${constant}" "${source}"
        ${ARGN})
    target_sources(${target} PRIVATE "${output}")
    target_compile_definitions(${target} PRIVATE "${definition}=\"${output}\"")
endfunction()

function(rb_compose_gl_shader_header output_variable)
    set(one_value_args OUTPUT NAMESPACE SHADER_DIR)
    set(multi_value_args ENTRIES PRELUDE DEPENDS)
    cmake_parse_arguments(PARSE_ARGV 1 shader "" "${one_value_args}" "${multi_value_args}")
    if(shader_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "unknown OpenGL shader arguments ${shader_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT shader_OUTPUT OR NOT shader_NAMESPACE OR NOT shader_SHADER_DIR OR
       NOT shader_ENTRIES)
        message(FATAL_ERROR "OpenGL shader header needs OUTPUT, NAMESPACE, SHADER_DIR and ENTRIES")
    endif()

    string(JOIN "," manifest ${shader_ENTRIES})
    set(prelude_argument)
    if(shader_PRELUDE)
        string(JOIN "," prelude ${shader_PRELUDE})
        set(prelude_argument "-DPRELUDE=${prelude}")
    endif()

    set(dependencies)
    foreach(entry IN LISTS shader_ENTRIES)
        string(REGEX REPLACE "^[^=]+=" "" source "${entry}")
        list(APPEND dependencies "${shader_SHADER_DIR}/${source}")
    endforeach()
    foreach(dependency IN LISTS shader_DEPENDS)
        if(IS_ABSOLUTE "${dependency}")
            list(APPEND dependencies "${dependency}")
        else()
            list(APPEND dependencies "${shader_SHADER_DIR}/${dependency}")
        endif()
    endforeach()

    set(composer "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/RabbetShaderCompose.cmake")
    cmake_path(GET shader_OUTPUT PARENT_PATH output_dir)
    add_custom_command(
        OUTPUT "${shader_OUTPUT}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dir}"
        COMMAND ${CMAKE_COMMAND}
            "-DSHADER_DIR=${shader_SHADER_DIR}"
            "-DOUTPUT=${shader_OUTPUT}"
            "-DNAMESPACE=${shader_NAMESPACE}"
            "-DMANIFEST=${manifest}"
            ${prelude_argument}
            "-DDEPFILE=${shader_OUTPUT}.d"
            -P "${composer}"
        DEPENDS
            ${dependencies}
            "${composer}"
            "${CMAKE_CURRENT_LIST_FILE}"
            "${CMAKE_CURRENT_FUNCTION_LIST_FILE}"
        DEPFILE "${shader_OUTPUT}.d"
        COMMENT "Composing the ${shader_NAMESPACE} OpenGL shader header"
        VERBATIM)

    set(${output_variable} "${shader_OUTPUT}" PARENT_SCOPE)
endfunction()

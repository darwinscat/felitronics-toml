# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

# felitronics_toml_embed(<target> INPUT <file.toml> NAMESPACE <ns> NAME <name> [HEADER <path>])
#
# Embeds a TOML document into <target> as constexpr data: at build time felitronics_toml2cpp turns INPUT into the
# header HEADER (default <name>.h) under the target's include path, and after #include "<name>.h" the target has
# <ns>::<name>, a constexpr felitronics::toml::embedded::Document. The header is regenerated when the document or
# the tool changes. A document the parser refuses fails the build with its file, line, column and error code.
#
# The tool runs on the build machine. When cross-compiling without an emulator, build felitronics_toml2cpp for the
# host first and set FELITRONICS_TOML2CPP_EXECUTABLE to it.
function(felitronics_toml_embed target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "INPUT;NAMESPACE;NAME;HEADER" "")
    foreach(required INPUT NAMESPACE NAME)
        if(NOT arg_${required})
            message(FATAL_ERROR "felitronics_toml_embed(${target}): ${required} is required")
        endif()
    endforeach()
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "felitronics_toml_embed(${target}): unexpected arguments ${arg_UNPARSED_ARGUMENTS}")
    endif()
    if(NOT arg_HEADER)
        set(arg_HEADER "${arg_NAME}.h")
    endif()
    if(FELITRONICS_TOML2CPP_EXECUTABLE)
        set(tool "${FELITRONICS_TOML2CPP_EXECUTABLE}")
    else()
        get_property(tool GLOBAL PROPERTY FELITRONICS_TOML2CPP_TARGET)
        if(NOT tool OR NOT TARGET "${tool}")
            message(FATAL_ERROR "felitronics_toml_embed(${target}): no felitronics_toml2cpp target; set FELITRONICS_TOML2CPP_EXECUTABLE")
        endif()
    endif()
    get_filename_component(input "${arg_INPUT}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
    set(directory "${CMAKE_CURRENT_BINARY_DIR}/felitronics_toml_embed/${target}")
    set(output "${directory}/${arg_HEADER}")
    get_filename_component(parent "${output}" DIRECTORY)
    file(MAKE_DIRECTORY "${parent}")
    # The tool deliberately leaves an equal header's timestamp alone. Mark a successful generation current so Make
    # does not repeat it while the unchanged header remains older than its input.
    add_custom_command(OUTPUT "${output}"
        COMMAND "${tool}" "${input}" "${output}" "${arg_NAMESPACE}" "${arg_NAME}"
        COMMAND "${CMAKE_COMMAND}" -E touch "${output}"
        DEPENDS "${input}" "${tool}"
        COMMENT "felitronics_toml2cpp ${arg_INPUT}"
        VERBATIM)
    target_sources(${target} PRIVATE "${output}")
    target_include_directories(${target} PRIVATE "${directory}")
    target_link_libraries(${target} PRIVATE felitronics::toml)
endfunction()

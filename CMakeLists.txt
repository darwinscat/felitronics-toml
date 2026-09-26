# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

cmake_minimum_required(VERSION 3.21)
project(felitronics_toml
    VERSION 0.1.0
    DESCRIPTION "A strict, deterministic TOML subset for C++20: parser and canonical writer, header-only"
    HOMEPAGE_URL "https://github.com/darwinscat/felitronics-toml"
    LANGUAGES CXX)

# ----------------------------------------------------------------------------------------------------
# Consume (the main path):
#   include(FetchContent)
#   FetchContent_Declare(felitronics_toml
#       GIT_REPOSITORY https://github.com/darwinscat/felitronics-toml.git
#       GIT_TAG        v0.1.0)
#   FetchContent_MakeAvailable(felitronics_toml)
#   target_link_libraries(app PRIVATE felitronics::toml)
#
# The target carries an include path and cxx_std_20, nothing else: no compile options, no warnings and no
# definitions reach a consumer. The strict warning set below applies to this repository's own tests only.
# ----------------------------------------------------------------------------------------------------

include(GNUInstallDirs)

add_library(felitronics_toml INTERFACE)
add_library(felitronics::toml ALIAS felitronics_toml)
set_target_properties(felitronics_toml PROPERTIES EXPORT_NAME toml)
target_include_directories(felitronics_toml INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(felitronics_toml INTERFACE cxx_std_20)

option(FELITRONICS_TOML_BUILD_TESTS "Build and register the felitronics-toml test suites" "${PROJECT_IS_TOP_LEVEL}")
option(FELITRONICS_TOML_FUZZ "Build the libFuzzer + ASan + UBSan target (native Clang with libFuzzer)" OFF)
option(FELITRONICS_TOML_INSTALL "Generate install rules and a CMake package config" "${PROJECT_IS_TOP_LEVEL}")

if(FELITRONICS_TOML_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(FELITRONICS_TOML_FUZZ)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR EMSCRIPTEN)
        message(FATAL_ERROR "FELITRONICS_TOML_FUZZ requires a native Clang that ships the libFuzzer runtime")
    endif()
    # Arbitrary bytes must either return an error or yield a tree whose canonical text reparses to the same
    # value and writes the same bytes. Exceptions and RTTI off, every sanitizer finding fatal.
    add_executable(felitronics_toml_fuzz tools/fuzz_parse.cpp)
    target_link_libraries(felitronics_toml_fuzz PRIVATE felitronics::toml)
    target_compile_options(felitronics_toml_fuzz PRIVATE -fsanitize=fuzzer,address,undefined
        -fno-sanitize-recover=all -fno-omit-frame-pointer -fno-exceptions -fno-rtti -ffp-contract=off)
    target_link_options(felitronics_toml_fuzz PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

if(FELITRONICS_TOML_INSTALL)
    include(CMakePackageConfigHelpers)
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(TARGETS felitronics_toml EXPORT felitronics_tomlTargets)
    install(EXPORT felitronics_tomlTargets
        NAMESPACE felitronics::
        DESTINATION ${CMAKE_INSTALL_DATADIR}/felitronics_toml/cmake)
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/felitronics_tomlConfig.cmake
        "include(\"\${CMAKE_CURRENT_LIST_DIR}/felitronics_tomlTargets.cmake\")\n")
    write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/felitronics_tomlConfigVersion.cmake
        COMPATIBILITY SameMinorVersion ARCH_INDEPENDENT)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/felitronics_tomlConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/felitronics_tomlConfigVersion.cmake
        DESTINATION ${CMAKE_INSTALL_DATADIR}/felitronics_toml/cmake)
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/felitronics_toml)
endif()

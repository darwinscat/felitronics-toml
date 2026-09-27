# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

# A project that uses felitronics-toml the way an application does: from an installed package (find_package), or
# from a source tree (-DFELITRONICS_TOML_SOURCE=<path>, as FetchContent would add it), with a document embedded
# by felitronics_toml_embed(). CI builds and runs it both ways.
cmake_minimum_required(VERSION 3.21)
project(felitronics_toml_consumer LANGUAGES CXX)

if(FELITRONICS_TOML_SOURCE)
    add_subdirectory(${FELITRONICS_TOML_SOURCE} felitronics_toml)
else()
    # Keep the older request: additive 0.3 releases must still satisfy 0.2 consumers.
    find_package(felitronics_toml 0.2 REQUIRED)
endif()

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE felitronics::toml)
felitronics_toml_embed(consumer INPUT factory.toml NAMESPACE presets NAME factory)

enable_testing()
add_test(NAME consumer COMMAND consumer)
set_tests_properties(consumer PROPERTIES PASS_REGULAR_EXPRESSION "ceiling -1\\.5 from layer 2, 2 bands, 1 warning")

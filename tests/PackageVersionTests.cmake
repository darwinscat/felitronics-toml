# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/felitronics_toml_version.cmake")

function(check_version file request compatible exact)
    set(PACKAGE_FIND_VERSION "${request}")
    string(REPLACE "." ";" parts "${request}")
    list(GET parts 0 PACKAGE_FIND_VERSION_MAJOR)
    include("${file}")
    if(NOT "${PACKAGE_VERSION_COMPATIBLE}" STREQUAL "${compatible}")
        message(FATAL_ERROR "${PACKAGE_VERSION} compatibility with ${request}: ${PACKAGE_VERSION_COMPATIBLE}, expected ${compatible}")
    endif()
    if(exact AND NOT PACKAGE_VERSION_EXACT)
        message(FATAL_ERROR "${PACKAGE_VERSION} must exactly match ${request}")
    endif()
endfunction()

check_version("${VERSION_FILE}" 0.2 TRUE FALSE)
check_version("${VERSION_FILE}" 0.2.9 TRUE FALSE)
check_version("${VERSION_FILE}" 0.3.0 TRUE TRUE)
check_version("${VERSION_FILE}" 0.3.1 FALSE FALSE)
check_version("${VERSION_FILE}" 1.0 FALSE FALSE)
# Use the release's production rule to exercise the future-major boundary as well.
felitronics_toml_package_version("${CMAKE_CURRENT_BINARY_DIR}/future-major-version.cmake" 1.0.0)
check_version("${CMAKE_CURRENT_BINARY_DIR}/future-major-version.cmake" 0.2 FALSE FALSE)
check_version("${CMAKE_CURRENT_BINARY_DIR}/future-major-version.cmake" 0.3 FALSE FALSE)
check_version("${CMAKE_CURRENT_BINARY_DIR}/future-major-version.cmake" 1.0.0 TRUE TRUE)
message(STATUS "Package version compatibility passed")

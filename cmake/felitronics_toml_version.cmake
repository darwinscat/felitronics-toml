# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
include(CMakePackageConfigHelpers)

function(felitronics_toml_package_version output version)
    # Additive releases satisfy earlier requests with the same major, including 0.2 -> 0.3.
    # A newer major never satisfies an older-major request: 1.x cannot satisfy 0.x.
    write_basic_package_version_file("${output}" VERSION "${version}"
        COMPATIBILITY SameMajorVersion ARCH_INDEPENDENT)
endfunction()

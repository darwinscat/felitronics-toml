# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

# The strict warning set, for THIS directory's targets only (never the consumer's): the header is compiled
# here under -Werror (/WX), so a warning it would raise in someone else's strict build fails CI first.
if(MSVC)
    set(felitronics_toml_warnings /W4 /WX /permissive- /utf-8)
else()
    set(felitronics_toml_warnings
        -Werror -Wall -Wextra -Wpedantic
        -Wconversion -Wsign-conversion -Wfloat-equal -Wshadow -Wcast-align
        -Wzero-as-null-pointer-constant -Wunreachable-code -Wswitch-enum -Wextra-semi
        $<$<CXX_COMPILER_ID:Clang,AppleClang>:-Wshorten-64-to-32;-Wimplicit-float-conversion;-Wconditional-uninitialized>
        # The suites pin binary64 bits. Contraction would let a compiler fuse a multiply into an add in the
        # test's own oracle; off states that instead of inheriting each compiler's default.
        -ffp-contract=off)
endif()

# CMake puts /EHsc into CMAKE_CXX_FLAGS for every MSVC target, and a target cannot take it back: adding /EHs-c-
# after it only earns a D9025 "overriding" warning. So this directory (the variable is directory-scoped) drops
# it and states exception handling per target.
if(MSVC)
    string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

# felitronics_toml_test(<name> <source> [NO_EXCEPTIONS])
function(felitronics_toml_test name source)
    cmake_parse_arguments(PARSE_ARGV 2 arg "NO_EXCEPTIONS" "" "")
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE felitronics::toml)
    target_compile_options(${name} PRIVATE ${felitronics_toml_warnings})
    if(arg_NO_EXCEPTIONS AND MSVC)
        target_compile_options(${name} PRIVATE /EHs-c- /GR-)
        target_compile_definitions(${name} PRIVATE _HAS_EXCEPTIONS=0)
    elseif(arg_NO_EXCEPTIONS)
        target_compile_options(${name} PRIVATE -fno-exceptions -fno-rtti)
    elseif(MSVC)
        target_compile_options(${name} PRIVATE /EHsc)
    endif()
    if(EMSCRIPTEN)
        # The limit tests build megabyte documents and a 65536-entry tree. Measured on emsdk 6.0.9, the grammar
        # suite needs between 12 and 16 MB: it fits the fixed 16 MB default heap, but with no margin to speak of.
        target_link_options(${name} PRIVATE -sALLOW_MEMORY_GROWTH=1)
    endif()
    add_test(NAME ${name} COMMAND ${name})
endfunction()

felitronics_toml_test(felitronics_toml_grammar_tests GrammarTests.cpp)
felitronics_toml_test(felitronics_toml_decimal_tests DecimalTests.cpp)
felitronics_toml_test(felitronics_toml_properties_tests PropertiesTests.cpp)

# The canonical bytes of the generated corpus, hashed (FNV-1a). Every platform must print this same number:
# a writer or a decimal conversion that differed anywhere would change it. A deliberate change to the writer
# or to the generator changes it too, and then this is the line to update.
set(FELITRONICS_TOML_CORPUS_DIGEST 6415677334463673946)
set_tests_properties(felitronics_toml_properties_tests PROPERTIES PASS_REGULAR_EXPRESSION
    "canonical corpus FNV-1a: ${FELITRONICS_TOML_CORPUS_DIGEST}\r?\n\r?\n[0-9]+ checks, 0 failures\r?\nALL TESTS PASSED")

# The README example, run twice: as written, and with exceptions and RTTI switched off.
felitronics_toml_test(felitronics_toml_example Example.cpp)
felitronics_toml_test(felitronics_toml_example_no_exceptions Example.cpp NO_EXCEPTIONS)
set(example_output [=[ceiling = -1000 / 10^3
name = "Warm master"
ceiling = -1.000
gain = 0.50

[limiter]
release = 0.050
]=])
string(REGEX REPLACE "([][+.*?()^$|\\])" "\\\\\\1" example_regex "${example_output}")
string(REPLACE "\n" "\r?\n" example_regex "${example_regex}")
set_tests_properties(felitronics_toml_example felitronics_toml_example_no_exceptions PROPERTIES
    PASS_REGULAR_EXPRESSION "^${example_regex}$")

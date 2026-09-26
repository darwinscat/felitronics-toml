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
# after it only earns a D9025 "overriding" warning. So this directory (the variables are directory-scoped) drops
# every whole /EH option token and states exception handling per target.
if(MSVC)
    foreach(flags CMAKE_CXX_FLAGS CMAKE_CXX_FLAGS_DEBUG CMAKE_CXX_FLAGS_RELEASE CMAKE_CXX_FLAGS_RELWITHDEBINFO CMAKE_CXX_FLAGS_MINSIZEREL)
        string(REGEX REPLACE "(^| )[/-]EH[^ ]*" "" ${flags} "${${flags}}")
    endforeach()
endif()

# felitronics_toml_test(<name> <source> [NO_EXCEPTIONS] [ARGS <arguments>...])
function(felitronics_toml_test name source)
    cmake_parse_arguments(PARSE_ARGV 2 arg "NO_EXCEPTIONS" "" "ARGS")
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
    add_test(NAME ${name} COMMAND ${name} ${arg_ARGS})
endfunction()

felitronics_toml_test(felitronics_toml_grammar_tests GrammarTests.cpp)
felitronics_toml_test(felitronics_toml_decimal_tests DecimalTests.cpp)
felitronics_toml_test(felitronics_toml_properties_tests PropertiesTests.cpp)
felitronics_toml_test(felitronics_toml_position_tests PositionTests.cpp)
felitronics_toml_test(felitronics_toml_overlay_tests OverlayTests.cpp)
felitronics_toml_test(felitronics_toml_schema_tests SchemaTests.cpp)

# The language-neutral conformance corpus (tests/corpus/README.md): the same files any other implementation
# of this subset can run to prove it matches, byte for byte and error position for error position.
felitronics_toml_test(felitronics_toml_corpus_tests CorpusTests.cpp ARGS ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
if(EMSCRIPTEN)
    target_link_options(felitronics_toml_corpus_tests PRIVATE -sNODERAWFS=1) # node reads the corpus from the host
endif()

# The canonical bytes of the generated corpus, hashed (FNV-1a). Every platform must print this same number:
# a writer or a decimal conversion that differed anywhere would change it. A deliberate change to the writer
# or to the generator changes it too, and then this is the line to update.
set(FELITRONICS_TOML_CORPUS_DIGEST 15547082836514840367)
set_tests_properties(felitronics_toml_properties_tests PROPERTIES PASS_REGULAR_EXPRESSION
    "canonical corpus FNV-1a: ${FELITRONICS_TOML_CORPUS_DIGEST}\r?\n\r?\n[0-9]+ checks, 0 failures\r?\nALL TESTS PASSED")

# Every valid corpus document embedded by felitronics_toml2cpp at build time, then compared with what parse() makes
# of the same file: types, values, key order, styles and positions. One translation unit per document, so a parallel
# build compiles them side by side, and a registry that lists them.
target_compile_options(felitronics_toml2cpp PRIVATE ${felitronics_toml_warnings})
felitronics_toml_test(felitronics_toml_embed_tests EmbedTests.cpp ARGS ${CMAKE_CURRENT_SOURCE_DIR}/corpus)
file(GLOB embed_documents CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/corpus/valid/*.toml)
list(FILTER embed_documents EXCLUDE REGEX "\\.canonical\\.toml$")
set(embed_declarations "")
set(embed_entries "")
foreach(document IN LISTS embed_documents)
    get_filename_component(stem "${document}" NAME_WE)
    string(MAKE_C_IDENTIFIER "${stem}" id)
    felitronics_toml_embed(felitronics_toml_embed_tests INPUT "${document}" NAMESPACE felitronics_toml_corpus NAME ${id}
        HEADER corpus/${id}.h)
    file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embed/${id}.cpp @ONLY CONTENT
"#include \"corpus/${id}.h\"
const felitronics::toml::embedded::Document& embedded_${id}();
const felitronics::toml::embedded::Document& embedded_${id}() { return felitronics_toml_corpus::${id}; }
")
    target_sources(felitronics_toml_embed_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/embed/${id}.cpp)
    string(APPEND embed_declarations "const felitronics::toml::embedded::Document& embedded_${id}();\n")
    string(APPEND embed_entries "        { \"${stem}\", &embedded_${id}() },\n")
endforeach()
file(CONFIGURE OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/embed/registry.cpp @ONLY CONTENT
"#include \"EmbeddedCorpus.h\"
${embed_declarations}
std::vector<EmbeddedDocument> embeddedCorpus()
{
    return {
${embed_entries}    };
}
")
target_sources(felitronics_toml_embed_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/embed/registry.cpp)
target_include_directories(felitronics_toml_embed_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(EMSCRIPTEN)
    target_link_options(felitronics_toml_embed_tests PRIVATE -sNODERAWFS=1)
endif()

# A refused document fails the build with the position in the form compilers use, and a bad command line with usage.
add_test(NAME felitronics_toml2cpp_refuses_a_document
    COMMAND felitronics_toml2cpp ${CMAKE_CURRENT_SOURCE_DIR}/corpus/invalid/duplicate-key.toml ${CMAKE_CURRENT_BINARY_DIR}/refused.h ns name)
set_tests_properties(felitronics_toml2cpp_refuses_a_document PROPERTIES WILL_FAIL TRUE)
add_test(NAME felitronics_toml2cpp_names_the_position
    COMMAND felitronics_toml2cpp ${CMAKE_CURRENT_SOURCE_DIR}/corpus/invalid/inline-trailing-comma.toml ${CMAKE_CURRENT_BINARY_DIR}/refused.h ns name)
set_tests_properties(felitronics_toml2cpp_names_the_position PROPERTIES
    PASS_REGULAR_EXPRESSION "inline-trailing-comma\\.toml:1:8: error: ExpectedKey")
add_test(NAME felitronics_toml2cpp_checks_names COMMAND felitronics_toml2cpp in.toml out.h "not a namespace" name)
set_tests_properties(felitronics_toml2cpp_checks_names PROPERTIES WILL_FAIL TRUE)

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

# Reseeds tests/corpus/ from the cases above; not part of the build (see tools/make_corpus.cpp).
add_executable(felitronics_toml_make_corpus EXCLUDE_FROM_ALL ${PROJECT_SOURCE_DIR}/tools/make_corpus.cpp)
target_include_directories(felitronics_toml_make_corpus PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(felitronics_toml_make_corpus PRIVATE felitronics::toml)
target_compile_options(felitronics_toml_make_corpus PRIVATE ${felitronics_toml_warnings})
if(MSVC)
    target_compile_options(felitronics_toml_make_corpus PRIVATE /EHsc)
endif()

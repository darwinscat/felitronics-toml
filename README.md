<!-- SPDX-License-Identifier: MIT -->
# felitronics-toml

**A strict, deterministic subset of TOML for C++20: a parser and a canonical writer in one header, with no
dependencies.**

For configuration and project files that must read as the same values on every platform and save as the same
bytes every time: a plugin on macOS and Windows, a Linux build server, the same code compiled to WebAssembly.

- **Exact decimals.** `-1.250` is stored as mantissa −1250 at scale 3. It keeps its scale when written back,
  and converts to `double` with one correctly rounded division, so every platform gets the same bits. No
  `strtod`, no `from_chars`, no locale.
- **Canonical output.** The same tree always writes the same bytes, in insertion order. Files diff cleanly.
- **Errors as data.** A stable code plus line and column, so your UI shows its own localized message and
  highlights the spot. The column counts characters: a Cyrillic letter, a CJK character or an emoji is one
  column, whatever its UTF-8 length. No English strings inside, no exceptions.
- **Bounded.** 1 MiB documents, 16 levels, fixed limits on keys, strings, arrays and entries. No recursion
  deepens with the input, so hostile input gets an error, not a stack overflow.
- **Small enough to specify completely.** The whole grammar fits on one page, and a
  [conformance corpus](tests/corpus/README.md) lets an implementation in any other language prove it matches.

One header, `<felitronics/toml/Toml.h>`, standard library only. No exceptions, no RTTI, no iostreams, no mutable
globals.

## Example

```cpp
#include <felitronics/toml/Toml.h>
#include <cstdio>

using namespace felitronics::toml;

int main()
{
    auto result = parse ("name = \"Warm master\"\n"
                         "ceiling = -1.000  # dBTP\n"
                         "\n"
                         "[limiter]\n"
                         "release = 0.050\n");
    if (const Error* e = std::get_if<Error> (&result))
    {
        // A stable code and a 1-based line:column, the column counted in characters. Your UI owns the message.
        std::printf ("%s at %u:%u\n", codeName (e->code), unsigned (e->line), unsigned (e->column));
        return 1;
    }
    Table& root = *std::get_if<Table> (&result);

    // find() is null for a missing key, get_if is null for another type. No exceptions anywhere.
    const Value* ceiling = root.find ("ceiling");
    const Decimal* d = ceiling ? std::get_if<Decimal> (&ceiling->data) : nullptr;
    if (d == nullptr) return 1;
    std::printf ("ceiling = %lld / 10^%u\n", static_cast<long long> (d->mantissa), unsigned (d->scale));
    // d->toDouble() is -1.0: one correctly rounded division, the same bits on every platform.

    // insert() appends and refuses a key that already exists. fromDouble() quantizes to a chosen scale.
    if (! root.insert ("gain", Decimal::fromDouble (0.5, 2))) return 1;

    // Canonical text: scalars first, then tables, each group in insertion order. Comments are not kept.
    const std::string text = write (root);
    std::fwrite (text.data(), 1, text.size(), stdout);
    return 0;
}
```

```text
ceiling = -1000 / 10^3
name = "Warm master"
ceiling = -1.000
gain = 0.50

[limiter]
release = 0.050
```

This is [`tests/Example.cpp`](tests/Example.cpp). ctest builds it twice, the second time with exceptions and
RTTI off, and compares the output byte for byte.

## Why it exists

Reading `gain = -1.25` looks trivial until the same file has to produce the same number everywhere. `strtod`
follows the C locale, so a German locale can stop at the comma. Floating-point `std::from_chars` is missing
from some toolchains still in use. A library that parses to `double` has already lost the difference between
`1.2` and `1.20`, so a load and save rewrites the user's file. Exceptions and RTTI are off in many audio and
WebAssembly builds. And a settings file that a user pastes from a forum post is untrusted input.

This library answers each of those with a rule instead of a dependency:

- A decimal is two exact integers, `mantissa / 10^scale`, with `|mantissa| <= 2^53` and 1 to 9 fractional
  digits. Both integers are exactly representable in binary64, so `toDouble()` is a single IEEE division,
  correctly rounded: the same bits as a correctly rounded `strtod` of the text, `-0.0` included, on every
  platform. The header refuses to compile where `double` is not IEEE binary64, or where `double` arithmetic
  runs in x87 extended precision (32-bit x86 without SSE2), which rounds twice and can land one bit off.
- The writer spells decimals from their integers and keeps scale and negative zero: `1.2300` stays `1.2300`.
- Errors are 30 enum codes with a 1-based line and a 1-based column counted in code points, so a position
  means the same thing to a person reading Ukrainian, Japanese or Arabic as to one reading English.
  `codeName()` gives a stable identifier for logs; the message a user reads is yours to write, in their
  language.
- Every resource has an inclusive limit and its own error code, and the canonical form of any accepted
  document is guaranteed to fit the same limits, so everything `parse` accepts can be written and read back.
- Tables keep insertion order, and the writer emits scalars, then tables, then arrays of tables, each group
  in that order, so a program controls how its files look.

## The subset

| TOML 1.0 | Here |
|---|---|
| Basic strings `"..."`, every escape | Yes. UTF-8 is validated; raw control characters are refused |
| Literal strings `'...'`, multi-line strings | No |
| Integers | Decimal digits only, signed 64-bit. No leading zeros, no `_`, no hex, octal or binary |
| Floats | Exact decimals only: 1 to 9 fractional digits, `\|mantissa\| <= 2^53`. No exponent, `inf`, `nan` |
| Booleans | Yes |
| Offset and local dates and times | No |
| Arrays | One scalar type per array, no nesting. Trailing comma and comments inside are fine |
| Inline tables `{ ... }` | No |
| Tables, dotted keys, quoted keys | Yes, with TOML's rules on redefinition |
| Arrays of tables `[[...]]` | Yes, including nested ones |
| Comments | Accepted and dropped: keep anything that must survive a save in a string value |
| Line endings | LF or CRLF. A leading BOM is refused |
| Limits | 1 MiB document, 16-part key paths, 256-byte keys, 64 KiB strings, 65536 array items, 65536 entries |

Anything outside the subset is an error with a position, never a silently skipped setting.
[`docs/TOML-SUBSET.md`](docs/TOML-SUBSET.md) is the complete contract: the grammar, the decimal rule, every
limit and error code, and exactly where each error points.

## How it is tested

Four suites, **117,286 checks** on every platform:

| Suite | Checks | What |
|---|---:|---|
| grammar | 437 | every error code at its exact position, every limit at the limit and one past it, hostile bytes in every context, columns after multi-byte text |
| decimal | 90,191 | 90,000 seeded rationals and every edge, against an oracle that uses integer long division only |
| property | 26,137 | 512 generated trees: `parse(write(tree)) == tree`, and writing again gives the same bytes |
| corpus | 521 | the [conformance corpus](tests/corpus/README.md): 57 valid documents, 366 invalid ones |

The property suite hashes the canonical bytes of everything it writes, and ctest requires the same hash,
`15720607452201634850`, on every platform. Before this release all four suites passed, with that hash, on:

| Platform | Compiler | Builds |
|---|---|---|
| macOS 26.5, arm64 | Apple clang 21.0.0 | Release; Debug with ASan + UBSan |
| macOS 13.7.4, x86-64 | Apple clang 14.0.3 | `-O2` |
| Debian 13, x86-64 | GCC 14.2.0 | Release; Debug with ASan + UBSan + LeakSanitizer |
| Windows 11, x64 | MSVC 19.44, `/W4 /WX /permissive-` | Release; Debug with `/fsanitize=address` |
| WebAssembly, node 24.19 | Emscripten 6.0.9 | Release, exceptions and RTTI off |
| Debian 13, i686 (32-bit, emulated in Docker) | GCC 14.2.0, `-msse2 -mfpmath=sse` | Release |

All test code builds with warnings as errors (`-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
-Wshadow` and more on gcc and clang).

- **An independent reader.** Python's `tomllib` reads all 514 generated documents and every valid corpus
  document as the expected values, down to the bits of every decimal and the sign of zero. Checked with Python
  3.11, 3.13 and 3.14.
- **Fuzzing.** libFuzzer with ASan and UBSan: arbitrary bytes must be refused, or parse to a tree whose
  canonical text reads back as the same tree and writes the same bytes. A 10-minute run executed
  30,501,345 inputs with no finding; CI fuzzes for another minute on every push.
- **CI** repeats the Release suites on gcc 14, clang, Apple clang and MSVC, the sanitizer row, the wasm row,
  the `tomllib` checks on Python 3.11 and the fuzzer.

## Conformance corpus

[`tests/corpus/`](tests/corpus/README.md) is the contract as plain files: valid documents with their expected
trees (the tagged JSON of [toml-test](https://github.com/toml-lang/toml-test), plus a `decimal` tag) and
canonical text, and invalid documents with the exact code, line and column they must be refused with. It
covers every error code, every limit edge, hostile input and every construct. A TypeScript, Java or Rust
implementation of this subset runs the same files to prove it reads and writes exactly what this one does.

## Use it

CMake 3.21 or later and a C++20 compiler. With FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(felitronics_toml
    GIT_REPOSITORY https://github.com/darwinscat/felitronics-toml.git
    GIT_TAG        v0.1.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(felitronics_toml)

target_link_libraries(your_app PRIVATE felitronics::toml)
```

The target adds an include path and `cxx_std_20`, nothing else: no compile options, warnings or definitions
reach your build, and the tests are not built when the project is not the top level. After
`cmake --install`, `find_package(felitronics_toml 0.1)` provides the same `felitronics::toml` target. Or copy
`include/felitronics/toml/Toml.h` into your tree: it has no other files.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 tools/python-roundtrip.py build/tests/felitronics_toml_properties_tests   # tomllib, Python 3.11+
python3 tools/python-roundtrip.py --corpus tests/corpus
```

Issues and pull requests are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md). Changes are listed in
[CHANGELOG.md](CHANGELOG.md).

## License

[MIT](LICENSE). Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks.

---

Built for the Felitronics audio tools ([felitronics-core](https://github.com/darwinscat/felitronics-core),
[TabbyEQ](https://github.com/darwinscat/tabby-eq), [OrbitAmp](https://github.com/darwinscat/orbit-amp) and
friends) by Darwin's Cat / Lafox Records: [darwinscat.com](https://darwinscat.com) ·
[github.com/darwinscat](https://github.com/darwinscat)

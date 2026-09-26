<!-- SPDX-License-Identifier: MIT -->
# felitronics-toml

**A strict, deterministic subset of TOML for C++20: a parser and a canonical writer in one header, with no
dependencies. Optional layers read fields into your structs, merge documents, and compile a document into the
program as constexpr data.**

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
  deepens past the depth limit, so hostile input gets an error, not a stack overflow.
- **Positions on everything.** Every value, key and table remembers its line and column, so your own semantic
  errors ("gain out of range") point at the right spot, in the right file.
- **Hand-written shapes.** Inline tables and arrays of them, multi-line strings and `48_000` are read as TOML
  reads them, and a load and a save keep inline rows inline, one row per line. Comments are still not kept.
- **Typed reading.** `Schema.h` reads fields into your own structs with types, ranges, defaults and requirements,
  and reports every key it did not read, so a typo is an error (or a warning) instead of a silently ignored
  setting. No macros, no reflection.
- **Layers and embedding.** `overlay()` lays a user's file over defaults and remembers which layer each value came
  from. `felitronics_toml_embed()` compiles a document into the program as constexpr data: nothing parsed at run
  time.
- **Small enough to specify completely.** The whole grammar fits on one page, and a
  [conformance corpus](tests/corpus/README.md) lets an implementation in any other language prove it matches.

The parser and writer are one header, `<felitronics/toml/Toml.h>`, standard library only; `Schema.h` and
`Embedded.h` are optional layers on it. No exceptions, no RTTI, no iostreams, no mutable globals.

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

### Reading into your structs

```cpp
#include <felitronics/toml/Schema.h>

struct Band { std::int64_t frequency = 0; Decimal gain; };
struct Limiter { double ceiling = -1.0; int lookaheadMs = 5; };

Limiter limiter;
std::vector<Band> bands;
const Report report = read (root, [&] (Reader& in)
{
    in.table ("limiter", Need::Required, [&] (Reader& t)
    {
        t.required ("ceiling", limiter.ceiling, { -12.0, 0.0 });   // a double in [-12, 0]
        t.optional ("lookahead", limiter.lookaheadMs, { 0, 50 });  // keeps 5 when absent
    });
    in.tables ("bands", Need::Optional, [&] (Reader& row)          // [[bands]], or bands = [{ ... }, ...]
    {
        Band b;
        row.required ("frequency", b.frequency, { 20, 20000 });
        row.required ("gain", b.gain);                             // gain = 3 reads as 3.0
        bands.push_back (b);
    });
});
for (const Problem& p : report.problems)                           // UnknownKey limiter.ceilng at 3:1
    std::printf ("%s %s at %u:%u\n", faultName (p.fault), p.path.c_str(), unsigned (p.position.line), unsigned (p.position.column));
```

A schema is plain code, one function per struct if you like, and they compose by calling each other. Every
problem is a stable code (`Missing`, `WrongType`, `OutOfRange`, `UnknownKey`, `Refused`), the key path and the
position; `ReadOptions{Severity::Warning}` turns unknown keys into warnings. An integer is accepted where a decimal is
expected, losslessly: `3` reads as `3.0`.

### Layers

```cpp
auto defaults = parse (factoryText, 1), user = parse (userText, 2);   // a source number per document
const Table settings = overlay (std::get<Table> (defaults), std::get<Table> (user));
```

Tables merge key by key; anything else the user's file holds (a scalar, an array, an array of tables) replaces the
default whole. Every value keeps its position, source included, so `settings.find ("gain")->position.source` is 2
when the user set it, and a problem the reader reports names the file to fix.

### Inline tables

```toml
limiter = { ceiling = -1.0, release = 0.050 }
bands = [
    { frequency = 100, gain = 1.5 },
    { frequency = 8_000, gain = -2.0 },
]
```

This reads as the same data a `[limiter]` table and `[[bands]]` headers give, so your code reads it one way. The
parser marks the tables it read in braces (`Table::style`), and the writer keeps them that way: a load and a save
give the file back with each row on a line of its own (and `8_000` spelled `8000`).

### Embedding

```cmake
felitronics_toml_embed(my_app INPUT presets/factory.toml NAMESPACE presets NAME factory)
```

```cpp
#include "factory.h"                                                     // generated at build time

static_assert (presets::factory.root().find ("bands").size() == 2);     // constexpr: nothing parsed at run time
const Table factory = embedded::toTable (presets::factory.root(), 1);   // for the reader, or under overlay()
```

A document the parser refuses fails the build with `factory.toml:12:5: error: DuplicateKey`.
[`tests/consumer/`](tests/consumer) is a complete project that embeds a preset, lays a user's file over it and
reads the result into structs; CI builds it from the installed package and from the source tree.

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
- Errors are 32 enum codes with a 1-based line and a 1-based column counted in code points, so a position
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
| Multi-line basic strings `"""..."""` | Yes, with TOML's rules; CRLF inside reads as LF |
| Literal strings `'...'` and `'''...'''` | No |
| Integers | Decimal digits only, signed 64-bit, `_` between digits. No leading zeros, no hex, octal or binary |
| Floats | Exact decimals only: 1 to 9 fractional digits, `\|mantissa\| <= 2^53`, `_` between digits. No exponent, `inf`, `nan` |
| Booleans | Yes |
| Offset and local dates and times | No |
| Arrays | One type per array: a scalar type, or inline tables. No nested arrays. Trailing comma and comments inside are fine |
| Inline tables `{ ... }` | Yes, with TOML's rules: one line, no trailing comma, closed once defined |
| Tables, dotted keys, quoted keys | Yes, with TOML's rules on redefinition |
| Arrays of tables `[[...]]` | Yes, including nested ones |
| Comments | Accepted and dropped: keep anything that must survive a save in a string value |
| Line endings | LF or CRLF. A leading BOM is refused |
| Limits | 1 MiB document, 16-part key paths, 256-byte keys, 64 KiB strings, 65536 array items, 65536 entries |

Anything outside the subset is an error with a position, never a silently skipped setting.
[`docs/TOML-SUBSET.md`](docs/TOML-SUBSET.md) is the complete contract: the grammar, the decimal rule, every
limit and error code, and exactly where each error points.

## How it is tested

Eight suites, **118,196 checks** on every platform:

| Suite | Checks | What |
|---|---:|---|
| grammar | 572 | every error code at its exact position, every limit at the limit and one past it, hostile bytes in every context, columns after multi-byte text, inline tables, multi-line strings, underscores |
| decimal | 90,191 | 90,000 seeded rationals and every edge, against an oracle that uses integer long division only |
| property | 26,137 | 512 generated trees: `parse(write(tree)) == tree`, and writing again gives the same bytes |
| position | 187 | every construct at its exact position; every position of 128 generated documents found again without the parser |
| overlay | 18 | the merge rule, provenance, styles, empty layers, three layers in order |
| schema | 53 | whole documents into structs, every fault at its position, ranges across scales, integers as decimals |
| corpus | 745 | the [conformance corpus](tests/corpus/README.md): 72 valid documents, 427 invalid ones, 9 overlays, 9 schema cases |
| embedding | 293 | every valid corpus document embedded at build time, node for node against its parse, some with `static_assert` |

The property suite hashes the canonical bytes of everything it writes, and ctest requires the same hash,
`15547082836514840367`, on every platform. Before this release all the suites passed, with that hash, on:

| Platform | Compiler | Builds |
|---|---|---|
| macOS 26.5, arm64 | Apple clang 21.0.0 | Release; Debug with ASan + UBSan |
| Debian 13, x86-64 | GCC 14.2.0 | Release; Debug with ASan + UBSan + LeakSanitizer |
| Windows 11, x64 | MSVC 19.44, `/W4 /WX /permissive-` | Release; Debug with `/fsanitize=address` |
| WebAssembly, node 24.19 | Emscripten 6.0.9 | Release, exceptions and RTTI off; the embedding tool runs in node at build time |

All test code, and the headers the embedding tool generates, build with warnings as errors (`-Wall -Wextra
-Wpedantic -Wconversion -Wsign-conversion -Wshadow` and more on gcc and clang, `/W4 /WX` on MSVC).

- **An independent reader.** Python's `tomllib` reads all 514 generated documents and every valid corpus
  document as the expected values, down to the bits of every decimal and the sign of zero. The same script checks
  every corpus position against the text, merges every overlay case again, and reads every schema case again from
  the rules. Checked with Python 3.11, 3.13 and 3.14.
- **Fuzzing.** libFuzzer with ASan and UBSan: arbitrary bytes must be refused, or parse to a tree whose
  canonical text reads back as the same tree and writes the same bytes, where every value has a position and
  `overlay()` of an equal tree changes nothing. A 10-minute run executed 5,647,240 inputs with no finding; CI
  fuzzes for another minute on every push.
- **CI** repeats the Release suites on gcc 14, clang, Apple clang and MSVC, the sanitizer row, the wasm row,
  the `tomllib` checks on Python 3.11, the fuzzer, and a consumer project that embeds a document, from the
  installed package and from the source tree, on Linux, macOS and Windows.

## Conformance corpus

[`tests/corpus/`](tests/corpus/README.md) is the contract as plain files: valid documents with their expected
trees (the tagged JSON of [toml-test](https://github.com/toml-lang/toml-test), plus a `decimal` tag), canonical
text and positions, invalid documents with the exact code, line and column they must be refused with, layered
documents with their merged tree and the layer of every value, and schema cases with the values and problems they
read. It covers every error code, every limit edge, hostile input and every construct. A TypeScript, Java or Rust
implementation of this subset runs the same files to prove it reads and writes exactly what this one does.

## Use it

CMake 3.21 or later and a C++20 compiler. With FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(felitronics_toml
    GIT_REPOSITORY https://github.com/darwinscat/felitronics-toml.git
    GIT_TAG        v0.2.0
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(felitronics_toml)

target_link_libraries(your_app PRIVATE felitronics::toml)
felitronics_toml_embed(your_app INPUT presets.toml NAMESPACE presets NAME factory)   # optional
```

The target adds an include path and `cxx_std_20`, nothing else: no compile options, warnings or definitions
reach your build, and the tests are not built when the project is not the top level. After
`cmake --install`, `find_package(felitronics_toml 0.2)` provides the same `felitronics::toml` target, the
`felitronics_toml2cpp` tool and `felitronics_toml_embed()`. The tool runs on the build machine: FetchContent under
Emscripten builds it for wasm and runs it in node, a package installed from a cross build (Emscripten included) has
no tool, and any cross build without one names a build-machine copy in `FELITRONICS_TOML2CPP_EXECUTABLE`. Or
copy `include/felitronics/toml/` into your tree: `Toml.h` alone is the parser and writer.

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

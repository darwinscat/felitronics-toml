<!-- SPDX-License-Identifier: MIT -->
# Changelog

## v0.3.0 — Unreleased

- **`storageFor(text)`**, and **`storageFor(text, ReadStorage)`** in `Schema.h`, declare cumulative
  allocation bytes before parsing and typed reading. Allocation-free counting shares the parser's syntax
  path, uses the library's element sizes, and covers refused documents. Schema descriptions name possible
  missing required paths and additional refusals.
- The canonical-size validation shares the writer with a size-only output and no longer allocates a
  serialized document. Parser and report vectors grow by doubling. Parsing, errors and read results are unchanged.
- Allocation-counting tests cover the conformance corpus, generated and adversarial documents on every
  CI row, including MSVC Debug with checked iterators and wasm32. K = 2, with an MSVC iterator-proxy term;
  realistic cases also enforce an 8-fold tightness ceiling.

## v0.2.0 — 2026-09-26

The subset grows by four TOML 1.0 constructs, every value knows where it was written, and three optional layers
build on the parser: typed reading, layered documents and embedded documents.

- **Inline tables** `{ k = v }` and **arrays of them**, with TOML's rules: one line, no trailing comma, closed
  once defined, nested within the depth limit. An array of inline tables is one type, "table", and parses to
  `Tables`, the same data `[[headers]]` give. Two new error codes, appended to the enum:
  `UnterminatedInlineTable` and `ExpectedInlineTableSeparator`.
- **Multi-line basic strings** `"""..."""`, with TOML's rules; CRLF inside reads as LF, so a checkout cannot
  change a value.
- **Underscores** between digits of integers and decimals: `48_000`, `1_000.000_1`.
- **Positions.** `Value::position`, `Entry::keyPosition` and `Table::position` give the line and code point column
  where each was written, and `parse(text, source)` puts a source number in each. They take no part in equality.
  `Table::entry()` returns a whole entry; `Table::insert()` takes an optional key position.
- **Canonical form.** `Table::style` (`Header` or `Inline`) chooses how a table is written. The parser marks the
  tables it read in braces inline, so a load and a save keep them inline; an array of tables is inline when all
  its tables are, and at statement level puts one table on each line. A string that is a statement's whole value
  and contains a line feed is written as a multi-line string. The writer never emits underscores.
- **`overlay(base, top)`** merges tables key by key and replaces everything else whole; each value keeps its
  layer's position, so `position.source` tells where it came from.
- **`<felitronics/toml/Schema.h>`**: `Reader` reads fields into an application's structs with types, inclusive
  ranges, defaults and requirements, through `table()` and `tables()` callbacks, and reports keys nobody read as
  `UnknownKey`, an error or a warning. Problems are data: a `Fault`, the key path, the position. An integer reads
  losslessly where a decimal is expected: `n` is `n.0`.
- **`<felitronics/toml/Embedded.h>`, `felitronics_toml2cpp` and `felitronics_toml_embed()`**: a document compiled
  into the program as constexpr data at build time, walked by `embedded::View` in constant expressions, and copied
  into a `Table` by `embedded::toTable()`. The package installs the CMake function, and the tool when it was built
for the build machine; a cross build names one in `FELITRONICS_TOML2CPP_EXECUTABLE`.

Contract changes a port has to follow, all in the corpus:

- `a={}`, `a=1_000` and `a="""x"""` are valid now; their old invalid cases moved to `valid/`.
- A bare value's token also ends at `}`: `a=1}` is `TrailingCharacters` at 1:4, no longer `InvalidNumber` at 1:3.
- Canonical text changes for statement strings that contain a line feed, so the generated corpus documents and the
  pinned digest of the property suite changed: it is now `15547082836514840367`, which also covers the new inline
  tables in the generated trees.
- New in the corpus: `valid/<name>.positions.json` for every document under 64 KiB, `overlay/` and `schema/`.
  72 valid documents, 427 invalid ones, 9 overlays and 9 schema cases.
- CI builds and runs a consumer project from the installed package and from the source tree.

## v0.1.0 — 2026-09-26

The first release: `felitronics::toml`, a strict, deterministic subset of TOML 1.0 for C++20. A parser and a
canonical writer in one header, with no dependencies beyond the standard library.

- **The subset.** Basic strings with every escape, quoted and dotted keys, comments, signed 64-bit integers,
  exact decimals, booleans, strict scalar arrays, nested tables and arrays of tables. Everything else in
  TOML 1.0 is refused with a position, never skipped. The contract is `docs/TOML-SUBSET.md`.
- **Exact decimals.** A decimal keeps its mantissa, its scale and a negative zero. Converting to `double` is
  one correctly rounded division of two exact integers, the same bits on every platform, with no libc
  number conversion and no locale state. `Decimal::fromDouble` quantizes a computed value to a chosen scale.
- **Canonical writer.** Scalars, then tables, then arrays of tables, each group in insertion order. The same
  tree always writes the same bytes, and every document the parser accepts can be written and read back.
- **Errors as data.** 30 codes with a 1-based line and a 1-based column counted in code points (a tab, a
  Cyrillic letter or an emoji is one column), for localized messages. `codeName()` gives stable identifiers
  for logs.
- **Bounded.** 1 MiB documents, 16-part key paths, 256-byte keys, 64 KiB strings, 65536 array items and
  65536 entries, each with its own error code. Key paths are walked iteratively and the only recursion (the
  key index, the writer) is bounded by the limits, so hostile input cannot exhaust the stack.
- **No exceptions, no RTTI, no mutable globals.** Builds and runs with `-fno-exceptions -fno-rtti` and in
  WebAssembly. Requires IEEE binary64 evaluated as `double`: 32-bit x86 builds need SSE2 arithmetic, since
  x87 extended precision rounds a quotient twice.
- **Tests.** Four suites (grammar with every error code and limit, exact decimal rounding against an
  integer-only oracle, generated round trips with a pinned digest of the canonical bytes, the conformance
  corpus) plus the README example, built again without exceptions and RTTI.
- **Conformance corpus.** `tests/corpus/`: 57 valid documents with their expected trees and canonical text,
  and 366 invalid documents with their exact error code and position, for implementations in any language.
- **Tools.** A cross-check against Python's `tomllib` for the generated documents and the corpus, and a
  libFuzzer target with ASan and UBSan.
- **CI.** gcc 14, clang, Apple clang and MSVC; ASan, UBSan and LeakSanitizer; Emscripten in node; `tomllib`
  on Python 3.11; a fuzzing run on every push.

First written for the project files of the Felitronics mastering tools.

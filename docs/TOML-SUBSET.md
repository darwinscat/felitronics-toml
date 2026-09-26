<!-- SPDX-License-Identifier: MIT -->

# The `felitronics::toml` subset

`<felitronics/toml/Toml.h>` is a dependency-free, header-only C++20 parser and canonical
writer for the subset of [TOML 1.0](https://toml.io/en/v1.0.0) specified below: a
deterministic format for configuration and project files in audio, embedded, desktop and
WebAssembly applications, where the same text must read as the same values on every
platform and every save must produce the same bytes. This is offline, allocating document
I/O, not a streaming or real-time API. It does not preserve comments. Store version labels
and other information that must survive a save as string values; comments remain useful for
hand-edited or pasted input.

This document is the contract. Its executable form is the conformance corpus in
[`tests/corpus/`](../tests/corpus/README.md): valid documents with their expected trees and
canonical text, invalid documents with their exact error code and position.

## API and ownership

```cpp
#include <felitronics/toml/Toml.h>

using namespace felitronics::toml;
auto result = parse ("[settings]\ngain = -1.250\n");
if (const auto* root = std::get_if<Table> (&result))
{
    const std::string canonical = write (*root);
    // Hand canonical to the application's own persistence layer.
}
else
{
    const Error error = std::get<Error> (result);
    // Localize error.code and highlight error.line:error.column.
}

Table project;
const bool inserted = project.insert ("label", Value ("Version A"));
const Decimal gain = Decimal::fromDouble (-1.25, 3);
// Test gain.valid() before putting a conversion result in a tree.
```

`[[nodiscard]] ParseResult parse (std::string_view) noexcept` returns
`std::variant<Table, Error>`. `Error` contains only `Code`, `uint32_t line`, and
`uint32_t column`. `codeName(Code)` returns a stable enum spelling for logs and tests,
not a localized user message. There is no exception or RTTI dependency. Process-wide
allocation failure is not recoverable in a build without exceptions; the document limits
are checked independently of that.

A `Value` wraps a public `data` variant of `std::string`, `std::int64_t`, `Decimal`,
`bool`, `Array` (`std::vector<Value>`), `Table`, and `Tables` (`std::vector<Table>`).
An `Array` contains only one scalar alternative; an empty array has no element type.
`Tables` is a nonempty array of tables and is distinct from `Array`. Construct integers
explicitly as `std::int64_t` to distinguish them from booleans.

`Table::insert(string, Value)` appends and returns false on an existing key. `find`
returns a pointer or null. `entries()` exposes the insertion-ordered entries as a const
vector of `{key, value}`. A private AVL index over vector offsets bounds lookup and
insertion to O(log n) key comparisons even for deliberately sorted keys. Insertion can
invalidate pointers into the table's entries. The tree owns all its data; there are no
borrowed input views. Copies have their own indexes, and moved-from tables are reusable.

Tree equality compares mappings by key, independent of insertion order, and preserves
array order, scalar alternatives, decimal scale, and decimal negative zero. Formatting
order is observable separately through `entries()` and `write()`: it is not part of
TOML's mapping value. This distinction is necessary because the writer groups scalars,
plain tables and arrays of tables, even when callers inserted those groups interleaved.

## Accepted text

Text is UTF-8, with LF or CRLF line endings, optionally ending without a newline.
A leading UTF-8 BOM is refused. Space and tab are the only horizontal whitespace.
Blank lines and `#` comments through end of line are accepted, including trailing
comments and comments between array elements. A `#` inside a string is data.
Raw NUL, DEL, and C0 controls other than tab and line endings are errors everywhere,
including comments; CR must be followed by LF. Strings cannot contain raw line endings.
Valid non-ASCII Unicode scalars are allowed in strings, quoted keys and comments.
Unicode is preserved without normalization. U+FEFF inside a string or comment is data.

The following grammar is descriptive EBNF. `hws` means zero or more spaces/tabs;
`aws` additionally permits line endings and comments. A statement is followed by
horizontal whitespace, an optional comment, and a line ending or EOF.

```text
statement   = path hws "=" hws value
            | "[" hws path hws "]"
            | "[[" hws path hws "]]"
path        = key { hws "." hws key }
key         = bare-key | basic-string
bare-key    = ("A".."Z" | "a".."z" | "0".."9" | "_" | "-")+
value       = scalar | array
scalar      = basic-string | integer | decimal | "true" | "false"
integer     = ["+" | "-"] unsigned-int
unsigned-int = "0" | ("1".."9") { "0".."9" }
decimal     = ["+" | "-"] unsigned-int "." ("0".."9"){1,9}
array       = "[" aws [scalar {aws "," aws scalar} [aws ","]] aws "]"
```

Bare keys are nonempty; quoted keys may be empty. All-numeric bare keys are still
strings. Dots inside quotes are data, so `a."b.c"` has two path components. Keys are
case-sensitive. Quoted keys use exactly the same escapes as basic string values:
`\"`, `\\`, `\b`, `\t`, `\n`, `\f`, `\r`, `\uXXXX`, and `\UXXXXXXXX`.
Hex digits in escapes may be upper or lower case. Unicode escapes must name scalar
values (0 through U+10FFFF except U+D800–U+DFFF); surrogate pairs are not accepted.
Escaped NUL/control characters are valid string/key data. Raw tab is also valid.
All lengths below count decoded UTF-8 bytes, not Unicode characters or escape spelling.

Integers must fit signed int64, including -9223372036854775808 and 9223372036854775807.
Integer `-0` becomes integer zero. Leading zeros are forbidden in both integer and
fractional-number integer parts (`00`, `-01`, `01.2`). Fractional leading zeros are
allowed. Integer and decimal alternatives remain distinct, so `[1, 1.0]` is a mixed
array and is refused. Decimal scales may differ within a decimal array.

Table headers are absolute from the root; key/value paths are relative to the current
table. A header may create implicit parents: `[a.b]` followed by `[a]` is valid.
Each explicit plain table header may occur only once. Dotted assignments define their
intermediate tables, so `a.b=1` followed by `[a]` redefines a table and is refused.
A dotted assignment cannot reopen an explicitly declared child table. Values cannot
be used as tables, and table and array-of-tables declarations cannot replace each other.
Repeated `[[a.b]]` headers append elements. Any nested header through an array of tables
attaches to its latest element, and each element has its own table-definition state.
Duplicate keys are errors even when their quoted and bare spellings differ.

## Exact decimals

`Decimal { int64_t mantissa; uint8_t scale; bool negativeZero; }` represents
`mantissa / 10^scale`. Valid scales are 1 through 9 and valid mantissas satisfy
`|mantissa| <= 2^53` (9007199254740992). The scale retains trailing zeros: `1.2300`
has mantissa 12300 and scale 4. The bound applies before removing any trailing zeros;
a number with an oversized mantissa is refused even if reducing its scale would fit.
`negativeZero` may be true only for a zero mantissa. `-0.0` and `-0.000` retain both
their sign and their respective scales. Explicit `+` signs are not preserved.

`toDouble()` converts the two exact integers to binary64 and performs one division.
Under IEEE round-to-nearest, ties-to-even, that division correctly rounds the exact
rational. Its bits therefore equal those of a correctly rounded conversion of the
same decimal text. Neither integer conversion is lossy, and no libc number parser,
float `to_chars`/`from_chars`, locale, or extended-precision intermediate is involved.
The header asserts IEEE binary64, and that `double` arithmetic is evaluated as `double`
(`FLT_EVAL_METHOD` 0 or 1): x87 arithmetic rounds a quotient twice, first to 64 bits, and
`7832510068573872 / 10^8` then comes out one unit in the last place low. A 32-bit x86
build therefore needs SSE2 arithmetic (MSVC's default; `-msse2 -mfpmath=sse` for gcc and
clang). The contract excludes fast-math and changes to the floating-point rounding mode; `-ffp-contract=off` and the wasm no-exceptions/no-RTTI
settings are supported. No mutable globals or function-local statics are used.

`Decimal::fromDouble(double x, uint8_t scale)` returns a `Decimal`, rounding the
binary64 product `x * 10^scale` half away from zero. The multiplication itself can
round; this API quantizes the supplied binary64 value, not an unavailable original
base-ten spelling. It preserves negative zero, including negative inputs rounded to
zero. Nonfinite products, out-of-range products/mantissas and invalid scales return
`{0, 0, false}`, an invalid decimal. Call `valid()` to distinguish refusal. Invalid
manually constructed decimals also fail `valid()`; their `toDouble()` returns a quiet
NaN and the writer refuses them. No floating-to-integer cast occurs before range checks.

## Limits and positions

| Resource | Inclusive limit | Failure |
|---|---:|---|
| Input text | 1 MiB (1048576 bytes) | `DocumentLimit` at the first byte past the limit |
| Canonical output | 1 MiB | `CanonicalLimit` at input EOF, after syntax validation |
| Absolute key path | 16 components, including current-table prefix | `DepthLimit` at the next component |
| One decoded key | 256 UTF-8 bytes | `KeyLimit` at the character or escape that exceeds the limit |
| One decoded string | 65536 UTF-8 bytes | `StringLimit` at the character or escape that exceeds the limit |
| One scalar array | 65536 items | `ArrayLimit` at the next item |
| Total entries | 65536 | `EntryLimit` at the responsible key component |

Every key/value entry counts once, including implicit tables and array-of-tables
containers; each array-of-tables element counts once additionally. Scalar array items
do not consume entries. Consequently one array-of-tables container can hold at most
65535 empty elements before the overall entry limit intervenes. The root is not an
entry. Header/assignment paths are iterative; accepted trees have depth at most 16.
Index recursion is bounded by AVL height. Writer descent checks depth before entering
a child; it never recursively processes nested value arrays.

Canonical output has a separate size check because adding spaces and spelling escaped
keys can grow a compact input. A syntactically valid input that fits 1 MiB but whose
canonical form exceeds it receives `CanonicalLimit`. This additional acceptance rule
ensures that every successful `parse` result can be written and parsed again under the
same limits. Whitespace/comment documents and canonical documents succeed at exactly
1 MiB. Caller-built trees are subject to the same decoded, structural and output limits.

Positions use 1-based lines and 1-based columns counted in **code points**: column = the
1-based code point index within the line, whatever each character's UTF-8 length, and a tab
counts as one. A combining mark is a column of its own; columns are not grapheme clusters
and not display width. Stated on bytes, which is how to implement it: every byte that is not
a UTF-8 continuation byte (`10xxxxxx`) starts a column. For valid UTF-8 that is exactly the
code point index, and it stays well defined for `DocumentLimit`, whose text is never
decoded (the byte just past 1 MiB may fall inside a character). Every other error points
at the start of a character: a key or string limit crossed by a multi-byte character
points at that character. Only LF ends a line, so CRLF advances the
line once and its CR is the last column of its line. End of input is one column past the
last character. Only one error is returned.
A document-size error precedes parsing. Otherwise the parser stops at the first lexical
or syntax failure; an invalid encoding/control byte takes precedence at the same position.
A bare value (a number or a boolean) is judged as one whole token: when it runs into an
invalid encoding/control byte, that byte's error is the result, even if the characters
before it are already out of range (`a=0.0000000000` followed by byte 0x80 is
`InvalidUtf8` at 1:15, not `DecimalScale` at 1:14).
Numeric range/format failures point to the token's start; a tenth decimal fractional
digit points to that digit. Duplicate/conflict errors point to the offending key
component; a repeated table header points to its final component. Canonical-size
validation follows a complete successful syntax parse.

| Code | Failure kind |
|---|---|
| `DocumentLimit` | Input is larger than 1 MiB |
| `CanonicalLimit` | Canonical spelling would exceed 1 MiB |
| `Bom` | Leading UTF-8 BOM |
| `InvalidUtf8` | Invalid, overlong, truncated, surrogate or out-of-range UTF-8 sequence; points at where it starts |
| `InvalidControl` | Forbidden raw control byte |
| `BareCarriageReturn` | CR without LF |
| `ExpectedKey` | Missing or invalid path component |
| `KeyLimit` | Decoded key too long |
| `DepthLimit` | Absolute path too deep |
| `ExpectedEquals` | Missing `=` after a key path |
| `ExpectedValue` | Missing scalar/value |
| `UnsupportedValue` | Unsupported value introducer, including literal string, inline table or nested array |
| `UnterminatedString` | EOF or raw line ending before closing quote |
| `StringLimit` | Decoded string too long |
| `InvalidEscape` | Unknown escape letter; points at the letter |
| `InvalidUnicodeEscape` | Missing/nonhex digit (at that digit) or nonscalar value (at the backslash) |
| `InvalidNumber` | Invalid integer/decimal/boolean token spelling |
| `IntegerRange` | Integer outside int64 |
| `DecimalScale` | More than nine fractional digits |
| `DecimalRange` | Decimal mantissa outside ±2^53 |
| `ExpectedArraySeparator` | Expected comma or closing bracket |
| `UnterminatedArray` | EOF before closing array bracket |
| `MixedArray` | Scalar alternative differs from the first element |
| `ArrayLimit` | Too many array items |
| `ExpectedHeaderEnd` | Missing closing header bracket(s) |
| `TrailingCharacters` | Content after a completed statement on the same line |
| `DuplicateKey` | Repeated scalar/array key |
| `RedefinedTable` | Plain table defined more than once |
| `TableValueConflict` | Table, array-of-tables or scalar used in an incompatible role |
| `EntryLimit` | Too many entries/elements in the document |

Unsupported syntax is an error, never an ignored setting or a partial successful tree.
In particular, literal and multiline strings, nested/mixed arrays, inline tables,
hex/octal/binary integers, underscores, exponents, infinities, NaNs, dates and times are
unsupported. Depending on the point where a spelling leaves this grammar, the code may
be `UnsupportedValue`, `InvalidNumber`, `TrailingCharacters` or another specific syntax
code above. An application should display its own localized explanation and the error
location when a user pastes such TOML, and leave its existing state unchanged. This library
returns the code and position; it does not own that UI or perform migration.

## Canonical writing

`[[nodiscard]] std::string write(const Table&)` emits scalar/array entries first,
then plain subtables, then arrays of tables. Insertion order within each group is
preserved. Each array-of-tables element is emitted together with all its children
before the next element. Headers use full paths. Scalars always use plain keys under
the correct current header, with `key = value` spacing. Arrays use `[a, b]` on one line.
Empty plain tables produce headers; the empty root produces an empty string.

Non-bare keys are basic quoted strings. Strings retain UTF-8 and raw tabs, escape
quotes/backslashes, use the short escapes for backspace, LF, form feed and CR, and
uppercase `\u00XX` for other forbidden raw controls. Decimals retain their scale and
negative zero. All line endings are LF, every emitted statement ends with LF, and one
blank line separates tables. There are no comments or leading blank lines.

`writeChecked(const Table&)` returns `std::optional<std::string>` and refuses invalid
caller-built trees (including invalid UTF-8, invalid decimals, duplicate-free but
oversized tables, nested/mixed scalar arrays, or empty `Tables`). `write` returns an
empty string on the same refusal; use `writeChecked` when the empty-root distinction
matters. Construction does not silently clamp or repair data. Empty `Array{}` is
representable as `[]`; empty `Tables{}` is not representable by table headers.

## Verification

Four ctest suites and the README example cover the contract. The grammar suite asserts
every error code with its exact position, every limit at the limit and one past it, and
hostile bytes in every context. The decimal suite checks 90000 seeded rationals and every
edge against an oracle that uses integer binary long division and ties-to-even remainder
tests, independent of the production floating-point division. The property suite writes
512 seeded trees (every scale, signed zero, int64 edges, all escape classes, non-ASCII
text, all scalar arrays, nested tables and nested arrays of tables) and requires
`parse(write(tree)) == tree` and byte-identical rewriting; the FNV-1a digest of its
canonical bytes is pinned in ctest, so every platform is compared against the same number.
The corpus suite runs `tests/corpus/`. The example is built twice, the second time with
exceptions and RTTI off.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 tools/python-roundtrip.py build/tests/felitronics_toml_properties_tests
python3 tools/python-roundtrip.py --corpus tests/corpus
```

The Python check (3.11 or later) uses only `tomllib` and the standard library. The property
suite's `--dump` mode emits JSONL records containing each canonical text and its
independently serialized original tree; Python compares all types and values, including
binary64 bits and signed zero. In `--corpus` mode it reads every valid corpus document and
its canonical text as the expected tree. CI runs both.

```sh
cmake -S . -B build-fuzz -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++ \
  -DFELITRONICS_TOML_FUZZ=ON -DFELITRONICS_TOML_BUILD_TESTS=OFF
cmake --build build-fuzz --target felitronics_toml_fuzz
mkdir -p build-fuzz/corpus
build-fuzz/felitronics_toml_fuzz build-fuzz/corpus -max_total_time=600 -max_len=1048577 -print_final_stats=1
```

The optional target compiles with `-fsanitize=fuzzer,address,undefined`, exceptions and
RTTI off, and hard-failing UBSan. It requires a native Clang installation that actually
ships the libFuzzer runtime (Apple's does not; a Linux distribution's clang does). Arbitrary
bytes must either return an error or yield a tree whose canonical output reparses to the
same value and writes to identical bytes.

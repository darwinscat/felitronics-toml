<!-- SPDX-License-Identifier: MIT -->
# Conformance corpus

These files are the language-neutral contract of the subset described in
[`docs/TOML-SUBSET.md`](../../docs/TOML-SUBSET.md), and of the layers and typed reading built on it. Any
implementation of it (this C++ header, a TypeScript or Java port, a validator in some other language) proves that
it matches by running the same files and getting the same trees, the same canonical bytes, the same positions, and
the same error codes at the same positions.

```text
valid/<name>.toml              a document the subset accepts
valid/<name>.json              the tree it must parse to
valid/<name>.canonical.toml    what write() must produce, when that differs from <name>.toml
valid/<name>.positions.json    where every value and key was written (documents under 64 KiB)
invalid/<name>.toml            a document the subset refuses
invalid/<name>.json            {"code": "<Code>", "line": N, "column": M}, the only acceptable refusal
overlay/<name>.base.toml       a base layer, parsed with source 1
overlay/<name>.top.toml        a top layer, parsed with source 2
overlay/<name>.json            the tree overlay(base, top) must give
overlay/<name>.canonical.toml  what write() must produce for it
overlay/<name>.positions.json  its positions, each with the source of the layer it came from
schema/<name>.toml             a document read by a schema
schema/<name>.json             the schema, the values it reads and the problems it reports
```

## Rules

1. **Bytes, not text.** Hand every `.toml` file to the parser exactly as stored: no newline translation, no BOM
   stripping, no decoding step of your own. Some documents are deliberately malformed (bare CR, NUL, invalid
   UTF-8). `.gitattributes` keeps git from converting them on any checkout.
2. **Valid documents.** `parse(<name>.toml)` succeeds and equals the tree in `<name>.json`. Writing that tree
   produces `<name>.canonical.toml` byte for byte when the file exists, and `<name>.toml` itself when it does
   not (the document is already canonical). The canonical text parses to the same tree again.
3. **Invalid documents.** `parse(<name>.toml)` fails with exactly the code, line and column in `<name>.json`.
   Codes are the names in `docs/TOML-SUBSET.md`. Lines are 1-based, and only LF ends one. Columns are the
   1-based **code point** index within the line: a Cyrillic letter, a CJK character and a 4-byte emoji are one
   column each, a tab is one, a combining mark is one of its own. On bytes: every byte that is not a UTF-8
   continuation byte (`10xxxxxx`) starts a column. End of input is one column past the last character. Only
   the first error is reported.
4. **Positions.** When `<name>.positions.json` exists, it lists the position of every value in the parsed tree,
   depth first: the root, then each entry of a table in its order (its key, then its value's contents), each
   array item and each array-of-tables element. Each record is `{"path": [...], "key": [line, column], "at":
   [line, column]}`; the path holds keys and item indexes, and items and elements have no `"key"`. Lines and
   columns count as in rule 3; `docs/TOML-SUBSET.md` says where each kind of value is.
5. **Overlays.** Parse `<name>.base.toml` with source 1 and `<name>.top.toml` with source 2, and lay the top over
   the base (`docs/TOML-SUBSET.md`, "Layers"): the result equals `<name>.json`, writes `<name>.canonical.toml`
   byte for byte, and has the positions in `<name>.positions.json`, whose records also carry `"source"`, the
   layer each value and its key came from.
6. **Schemas.** `<name>.json` holds `unknownKeys` (`"error"` or `"warning"`), `fields`, `read` and `problems`.
   Each field is `{"key", "type"}` with `type` one of `string`, `integer`, `decimal`, `bool`, `array` (with `"of"`,
   the item type), `table` and `tables` (both with `"fields"`), plus optional `"optional": true` and `"min"` and
   `"max"`, inclusive bounds spelled as TOML values. Read the fields in order from `<name>.toml` as
   `docs/TOML-SUBSET.md` ("Typed reading") says: the values read, as a tree in the tagged JSON above, equal
   `read`, and the problems, in order, equal `problems`: `{"fault", "severity", "path", "line", "column"}`.

## Tree encoding

The tagged JSON of the community [toml-test](https://github.com/toml-lang/toml-test) suite, with one addition
for this subset's exact decimal:

| TOML value | JSON |
|---|---|
| string | `{"type": "string", "value": "<the string>"}` |
| integer | `{"type": "integer", "value": "-42"}`: decimal text, since int64 exceeds what many JSON readers hold exactly |
| decimal | `{"type": "decimal", "value": "-0.00"}`: the canonical spelling, see below |
| boolean | `{"type": "bool", "value": "true"}` or `"false"` |
| table | a JSON object, one member per key |
| array of scalars | a JSON array of tagged scalars; `[]` is the empty array |
| array of tables | a JSON array of objects (never empty) |

An inline table is a table, and an array of inline tables is an array of tables: the JSON does not say how a table
was spelled, the canonical text does. In a schema's `read` tree, `[]` is an array-of-tables field with no rows.

**Decimal.** toml-test would tag these `float`, but this subset has no binary floats: a decimal is an exact
`mantissa / 10^scale` whose scale is part of the value. The `value` text carries both. `"1.2300"` is mantissa
12300 at scale 4, `"-0.00"` is a negative zero at scale 2, and there are always 1 to 9 fractional digits. To
compare with a reader that turns decimals into binary64, convert the text with a correctly rounded conversion
(Python's `float()`, for example): that is exactly what `Decimal::toDouble()` returns.

A tagged scalar is an object with exactly the two members `type` and `value`, both JSON strings. A table's
members are always objects or arrays, never bare strings, so a table whose keys happen to be `type` and `value`
cannot be mistaken for a scalar. Member order in a JSON object means nothing: TOML tables are unordered
mappings. Order is pinned by the canonical text instead. Strings are raw UTF-8 in JSON, with `\"`, `\\` and
`\u00XX` for control characters (DEL included).

## What is in it

| Family | Documents |
|---|---:|
| A minimal witness for error codes, and which error wins when two compete | 28 invalid |
| Table ownership: redefinition, dotted keys against headers, tables against values and arrays of tables | 10 invalid |
| TOML 1.0 values outside the subset, and malformed numbers, strings and arrays | 30 invalid |
| Inline tables and arrays of them: the grammar, one line, no trailing comma, closed once defined, one type per array, the writer's layout | 5 valid, 36 invalid |
| Multi-line basic strings: the trimmed line ending, CRLF, line-ending backslashes, quotes before the closing three, the writer's spelling | 3 valid, 9 invalid |
| Underscores between digits, and every misplaced one | 1 valid, 13 invalid |
| Every resource limit, at the limit and at limit + 1, also met and crossed by multi-byte characters, nested inline tables and arrays of them, and the quotes that close a multi-line string | 19 valid, 20 invalid |
| Hostile input: 1 MiB of `[`, a 100000-part key path, a 100000-item array, an unterminated 900000-byte string | 4 invalid |
| 13 classes of invalid UTF-8, each in a comment, a key, a string, a header and an array | 65 invalid |
| 30 forbidden control bytes in a comment, a string and a key; bare CR | 92 invalid |
| An invalid byte (0x80, then 0x00) inserted at every offset of a document that uses every construct | 110 invalid |
| Error columns after Cyrillic, CJK, Arabic (right to left), a combining mark, a 4-byte emoji and a tab | 10 invalid |
| Accepted grammar: comments, CRLF, quoted and dotted keys, headers, arrays of tables, edges of every type, and non-ASCII text in values, quoted keys, headers and comments | 27 valid |
| Generated: the first 16 of the 512 documents the property suite generates, and its depth-16 tree | 17 valid |
| Overlays: scalars, tables, arrays and type changes across two layers, styles and order, empty layers, non-ASCII keys | 9 overlay |
| Schema cases: every field type, integers read as decimals, ranges, missing keys, wrong types and containers, unknown keys as errors and as warnings | 9 schema |

72 valid documents (52 of them with a separate canonical text, 63 with positions), 427 invalid ones, 9 overlays and
9 schema cases: 20.6 MB of files (0.9 MB compressed), since a document at the 1 MiB limit is, by nature, a
megabyte. Non-ASCII text is deliberate throughout: Latin-1, Greek, Armenian, Cyrillic, CJK, Arabic, combining marks
and emoji, in string values, quoted keys, table headers and comments.

Python's `tomllib`, an independent TOML 1.0 reader, reads all 72 valid documents and all 52 canonical texts as the
expected trees, and a check written in Python confirms that every position points at what its value starts with
(`python3 tools/python-roundtrip.py --corpus tests/corpus`). It merges every overlay again from the rule and
confirms each value's layer, and reads every schema case again from the rules of typed reading, problem for problem.
`tomllib` also rejects 385 of the 427 invalid documents. The other 42 are valid TOML 1.0 that this subset refuses
on purpose: its resource limits, literal strings, nested and mixed arrays (a table among scalars included),
hexadecimal, octal and binary integers, exponents, `inf`/`nan`, dates and times, integers beyond int64 and decimals
beyond its precision.

## Running it

```sh
ctest --test-dir build -R corpus --output-on-failure            # or:
build/tests/felitronics_toml_corpus_tests tests/corpus
python3 tools/python-roundtrip.py --corpus tests/corpus          # the tomllib cross-check
```

A port walks the directories and applies the rules above: `valid/` and `invalid/` test a parser and a writer
(rules 1 to 4), `overlay/` a port that offers layers (rule 5), and `schema/` one that offers typed reading (rule 6).
Nothing else is needed: no C++ and no build of this repository.

## Where the expectations came from

The corpus was seeded by [`tools/make_corpus.cpp`](../../tools/make_corpus.cpp). Every invalid case's code, line
and column is written by hand, in that program or in `tests/Fixtures.h` (the same values `tests/GrammarTests.cpp`
asserts), and so is every problem of a schema case; the program refuses to write a case the implementation
disagrees with. Valid trees, canonical texts, positions and merged overlays are this implementation's output,
checked independently in Python as above. From here on the files themselves are the contract: a new case is
a new pair of files, and changing an existing expectation changes the subset, which belongs in `CHANGELOG.md`.

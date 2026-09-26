<!-- SPDX-License-Identifier: MIT -->
# Contributing

Issues and pull requests are welcome: bug reports, a platform where something differs, a test that
should exist, a clearer sentence in the docs.

## What a pull request needs

- **Every CI row green.** gcc, clang, Apple clang, MSVC, the sanitizer row, wasm, the `tomllib`
  cross-check, the fuzzer and the package rows. A change that is right on four platforms and wrong on the fifth
  is wrong.
- **A test for any change in behaviour.** A new rule or a fixed bug comes with the case that shows it:
  in `tests/` for C++-specific behaviour, and in the conformance corpus (below) for anything a document
  can show.
- **No new warnings.** The suites build with `-Werror` (`/WX` on MSVC) and a strict warning set; the
  header has to stay clean under it.

Green CI and tests are necessary, not sufficient. Every merge is the maintainer's decision: nothing
lands on `main` unless the maintainer merges it.

## The subset stays small on purpose

The point of this library is that its grammar is small enough to specify completely, test completely,
and implement identically in another language. Features that widen the grammar (another string form,
exponents, dates, literal strings, a higher limit) need a discussion in an issue first, before any code.
The answer may well be no, and that is not a judgement of the idea: every addition is something every
port has to match forever.

## The conformance corpus

[`tests/corpus/`](tests/corpus/README.md) is the language-neutral form of the contract: documents with
their expected trees and canonical text, and refused documents with their exact error code and position.
Implementations in other languages prove they match by running it.

- A new case is a new set of files (`<name>.toml` + `<name>.json`, and `<name>.canonical.toml` when the
  document is not already canonical; `overlay/` and `schema/` cases have their own set, see the corpus README).
  Name it after what it shows.
- Changing an existing expectation changes the subset for every implementation. Say so in the pull
  request and in `CHANGELOG.md`.
- The expected tree must also read correctly in Python's `tomllib`
  (`python3 tools/python-roundtrip.py --corpus tests/corpus`), which CI checks.

## Running what CI runs

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
python3 tools/python-roundtrip.py build/tests/felitronics_toml_properties_tests
python3 tools/python-roundtrip.py --corpus tests/corpus
```

The sanitizer, wasm and fuzzing commands are in `.github/workflows/ci.yml` and `docs/TOML-SUBSET.md`.

## License

Contributions are accepted under the repository's [MIT license](LICENSE), the same terms it is
distributed under (inbound = outbound, as GitHub's terms of service state). There is no CLA and no DCO
sign-off.

#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
"""An independent reader for our output: Python's own tomllib. Python >= 3.11, standard library only.

python3 tools/python-roundtrip.py build/tests/felitronics_toml_properties_tests
    The property suite's --dump mode: JSONL records of every generated canonical document and the typed
    expectation serialized from the original C++ tree. Or: <test executable> --dump | python3 tools/python-roundtrip.py
python3 tools/python-roundtrip.py --corpus tests/corpus
    The conformance corpus (tests/corpus/README.md): every valid document and its canonical text must read, in
    tomllib, as the expected tree. Decimals compare as binary64 bits of float(<canonical text>); their scale is
    invisible to tomllib and is checked by the C++ runner. Invalid documents carry codes and positions of this
    subset, which tomllib cannot check: they are counted, and how many of them tomllib also rejects is reported.
"""
import json
import pathlib
import struct
import subprocess
import sys
import tomllib


def compare(actual, expected, path="root"):
    tag, value = expected
    wanted_type = (str, int, float, bool, list, dict, list)[tag]
    assert type(actual) is wanted_type, (path, type(actual), wanted_type)
    if tag == 2:
        assert struct.pack(">d", actual).hex() == value, (path, actual, value)
    elif tag == 5:
        assert set(actual) == {key for key, _ in value}, path
        for key, child in value:
            compare(actual[key], child, f"{path}.{key!r}")
    elif tag in (4, 6):
        assert len(actual) == len(value), path
        for i, (item, child) in enumerate(zip(actual, value)):
            compare(item, child, f"{path}[{i}]")
    else:
        assert actual == value, (path, actual, value)


def compare_tagged(actual, expected, path):
    if isinstance(expected, dict) and set(expected) == {"type", "value"} and all(isinstance(v, str) for v in expected.values()):
        kind, text = expected["type"], expected["value"]
        if kind == "string":
            same = type(actual) is str and actual == text
        elif kind == "integer":
            same = type(actual) is int and actual == int(text)
        elif kind == "decimal":
            same = type(actual) is float and struct.pack(">d", actual) == struct.pack(">d", float(text))
        elif kind == "bool":
            same = type(actual) is bool and actual == (text == "true")
        else:
            same = False
        assert same, (path, actual, expected)
    elif isinstance(expected, dict):
        assert type(actual) is dict and set(actual) == set(expected), path
        for key, child in expected.items():
            compare_tagged(actual[key], child, f"{path}.{key!r}")
    else:
        assert isinstance(expected, list) and type(actual) is list and len(actual) == len(expected), path
        for i, (item, child) in enumerate(zip(actual, expected)):
            compare_tagged(item, child, f"{path}[{i}]")


def check_corpus(root):
    valid = sorted(p for p in (root / "valid").glob("*.toml") if not p.name.endswith(".canonical.toml"))
    texts = 0
    for document in valid:
        expected = json.loads(document.with_suffix(".json").read_bytes().decode("utf-8"))
        for path in (document, document.with_name(document.stem + ".canonical.toml")):
            if path.exists():
                compare_tagged(tomllib.loads(path.read_bytes().decode("utf-8")), expected, path.name)
                texts += 1
    invalid = sorted((root / "invalid").glob("*.toml"))
    rejected = 0
    for path in invalid:
        try:
            tomllib.loads(path.read_bytes().decode("utf-8"))
        except (UnicodeDecodeError, tomllib.TOMLDecodeError, RecursionError):  # 3.14 caps key parts at 1000
            rejected += 1
    assert valid and invalid, "no corpus documents found"
    print(f"tomllib: {len(valid)} valid corpus documents ({texts} texts with their canonical forms) match their "
          f"expected trees; {len(invalid)} invalid documents, {rejected} of which tomllib rejects too")


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--corpus":
        check_corpus(pathlib.Path(sys.argv[2]))
        return
    if len(sys.argv) > 1:
        dump = subprocess.run([sys.argv[1], "--dump"], check=True, stdout=subprocess.PIPE).stdout.decode("utf-8")
        lines = dump.splitlines()
    else:
        lines = sys.stdin
    count = 0
    for line in lines:
        record = json.loads(line)
        compare(tomllib.loads(record["toml"]), record["expected"])
        count += 1
    assert count > 0, "no dumped documents"
    print(f"tomllib: {count} canonical documents accepted; all types, values and decimal bits match")


if __name__ == "__main__":
    main()

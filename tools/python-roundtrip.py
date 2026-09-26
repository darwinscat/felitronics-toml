#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
"""An independent reader for our output: Python's own tomllib. Python >= 3.11, standard library only.

python3 tools/python-roundtrip.py build/tests/felitronics_toml_properties_tests
    The property suite's --dump mode: JSONL records of every generated canonical document and the typed
    expectation serialized from the original C++ tree. Or: <test executable> --dump | python3 tools/python-roundtrip.py
"""
import json
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


def main():
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

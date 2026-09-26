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
import decimal
import json
import pathlib
import re
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


def character_at(lines, position, name):
    """The character at a 1-based line and a 1-based column counted in code points (Python indexes code points)."""
    line, column = position
    assert 1 <= line <= len(lines) and 1 <= column <= len(lines[line - 1]) + 1, (name, position)
    text = lines[line - 1]
    return text[column - 1] if column <= len(text) else "\n"


def check_positions(document, text, positions):
    """Each position must point at what its value starts with, found here without any C++: a quote for a string,
    a digit or sign for a number, t or f for a boolean, [ for an array, [ or { for a table (or its key, for a table
    only a dotted or header path implies), and the bare or quoted spelling for a key."""
    lines = text.split("\n")
    for entry in positions:
        path, at = entry["path"], entry["at"]
        value = document
        for component in path:
            value = value[component]
        if not path:
            assert at == [1, 1], (path, at)
            continue
        c = character_at(lines, at, path)
        if type(value) is str:
            ok = c == '"'
        elif type(value) in (int, float):
            ok = c.isdigit() or c in "+-"
        elif type(value) is bool:
            ok = c == ("t" if value else "f")
        elif type(value) is list:
            ok = c == "["
        else:
            ok = c in "[{" or at == entry.get("key")
        assert ok, (path, at, c)
        if "key" in entry:
            key = entry["key"]
            k = character_at(lines, key, path)
            spelled = lines[key[0] - 1][key[1] - 1:]
            assert k == '"' or spelled.startswith(path[-1]), (path, key, spelled[:20])
        else:
            assert isinstance(path[-1], int), path


def merge(base, top):
    """overlay(), written again from its rule: tables merge key by key, anything else from top replaces base's."""
    out = dict(base)
    for key, value in top.items():
        out[key] = merge(out[key], value) if type(out.get(key)) is dict and type(value) is dict else value
    return out


def lookup(document, path):
    """The value at path, or None when some step is missing or is not a table or array."""
    for component in path:
        if isinstance(component, int):
            if type(document) is not list or component >= len(document):
                return None
        elif type(document) is not dict or component not in document:
            return None
        document = document[component]
    return document


def expected_source(base, top, merged, path):
    """Which layer a value at path comes from, by the rule: arrays are replaced whole, so anything inside one is the
    array's; a table is base's when base has a table there (tables merge), top's otherwise; any other value is
    top's when top has something there, base's when it does not."""
    for i, component in enumerate(path):
        if isinstance(component, int):
            return expected_source(base, top, merged, path[:i])
    if type(lookup(merged, path)) is dict:
        return 1 if type(lookup(base, path)) is dict else 2
    return 2 if lookup(top, path) is not None else 1


def check_overlay(root):
    """Each case must read in tomllib, merge here to the expected tree, and each position's source must be the layer
    the rule names, which holds exactly that value there."""
    cases = sorted(p.name[:-len(".base.toml")] for p in (root / "overlay").glob("*.base.toml"))
    for name in cases:
        read = lambda suffix: (root / "overlay" / (name + suffix)).read_bytes().decode("utf-8")
        base, top = tomllib.loads(read(".base.toml")), tomllib.loads(read(".top.toml"))
        merged = merge(base, top)
        expected = json.loads(read(".json"))
        compare_tagged(merged, expected, name)
        compare_tagged(tomllib.loads(read(".canonical.toml")), expected, name + ".canonical.toml")
        for entry in json.loads(read(".positions.json")):
            path, source = entry["path"], entry["source"]
            assert source == expected_source(base, top, merged, path), (name, path, source)
            value = lookup(merged, path)
            assert type(value) is dict or lookup({1: base, 2: top}[source], path) == value, (name, path)
    assert cases, "no overlay cases found"
    return len(cases)


def key_text(key):
    """A key in a problem's path, spelled as the writer spells keys: bare when it can be, else a basic string."""
    if re.fullmatch(r"[A-Za-z0-9_-]+", key):
        return key
    short = {'"': '\\"', "\\": "\\\\", "\b": "\\b", "\n": "\\n", "\f": "\\f", "\r": "\\r"}
    out = ""
    for c in key:
        if c in short:
            out += short[c]
        elif (ord(c) < 0x20 and c != "\t") or ord(c) == 0x7F:
            out += f"\\u{ord(c):04X}"
        else:
            out += c
    return '"' + out + '"'


def join(path, key):
    return (path + "." if path else "") + key_text(key)


def convert(value, kind, field):
    """(fault, the value read as its tagged JSON) for one scalar, by the rules of docs/TOML-SUBSET.md."""
    low, high = field.get("min"), field.get("max")
    if kind == "string" or kind == "bool":
        ok = type(value) is (str if kind == "string" else bool)
        return (None, {"type": kind, "value": value if kind == "string" else str(value).lower()}) if ok else ("WrongType", None)
    if kind == "integer":
        if type(value) is not int:
            return "WrongType", None
        if (low is not None and value < int(low)) or (high is not None and value > int(high)):
            return "OutOfRange", None
        return None, {"type": "integer", "value": str(value)}
    # decimal: a decimal, or an integer n read as n.0 while |n| <= 2^53 / 10
    if type(value) is int:
        if abs(value) > 900719925474099:
            return "OutOfRange", None
        value = decimal.Decimal(f"{value}.0")
    elif type(value) is not decimal.Decimal:
        return "WrongType", None
    if (low is not None and value < decimal.Decimal(low)) or (high is not None and value > decimal.Decimal(high)):
        return "OutOfRange", None
    return None, {"type": "decimal", "value": format(value, "f")}


def read_fields(table, fields, path, problems, unknown):
    """Reader, written again from its rules: fields in order, a sub-table's or a row's problems when it is read, then
    the table's own unread keys. Returns the tagged tree of the values read."""
    out, used = {}, set()
    for field in fields:
        key, kind = field["key"], field["type"]
        at = join(path, key)
        if key not in table:
            if not field.get("optional", False):
                problems.append(("Missing", "error", at))
            continue
        used.add(key)
        value = table[key]
        if kind == "table":
            if type(value) is not dict:
                problems.append(("WrongType", "error", at))
            else:
                out[key] = read_fields(value, field["fields"], at, problems, unknown)
        elif kind == "tables":
            if value == []:
                out[key] = []
            elif type(value) is not list or not all(type(row) is dict for row in value):
                problems.append(("WrongType", "error", at))
            else:
                out[key] = [read_fields(row, field["fields"], f"{at}[{i}]", problems, unknown) for i, row in enumerate(value)]
        elif kind == "array":
            if type(value) is not list:
                problems.append(("WrongType", "error", at))
                continue
            items = []
            for i, item in enumerate(value):
                fault, read = convert(item, field["of"], field)
                if fault:
                    problems.append((fault, "error", f"{at}[{i}]"))
                    break
                items.append(read)
            else:
                out[key] = items
        else:
            fault, read = convert(value, kind, field)
            if fault:
                problems.append((fault, "error", at))
            else:
                out[key] = read
    problems.extend(("UnknownKey", unknown, join(path, key)) for key in table if key not in used)
    return out


def check_schema(root):
    """Each case must read in tomllib; reading it again here, from the rules, must give the same values and the same
    problems in the same order; and each problem's position must point at what it names: the value, the key, or the
    table that lacks a key."""
    cases = sorted((root / "schema").glob("*.toml"))
    for document in cases:
        text = document.read_bytes().decode("utf-8")
        spec = json.loads(document.with_suffix(".json").read_bytes().decode("utf-8"))
        problems = []
        values = read_fields(tomllib.loads(text, parse_float=decimal.Decimal), spec["fields"], "", problems, spec["unknownKeys"])
        assert values == spec["read"], (document.name, values, spec["read"])
        assert problems == [(p["fault"], p["severity"], p["path"]) for p in spec["problems"]], (document.name, problems)
        lines = text.split("\n")
        for p in spec["problems"]:
            c = character_at(lines, [p["line"], p["column"]], p["path"])
            spelled = lines[p["line"] - 1][p["column"] - 1:]
            if p["fault"] == "UnknownKey":
                ok = c == '"' or re.match(r"[A-Za-z0-9_-]", c)
            elif p["fault"] == "Missing":
                ok = [p["line"], p["column"]] == [1, 1] or c in "[{" or c == '"' or re.match(r"[A-Za-z0-9_-]", c)
            else:
                ok = c in '"[{tf+-' or c.isdigit()
            assert ok, (document.name, p, spelled[:20])
    assert cases, "no schema cases found"
    return len(cases)


def check_corpus(root):
    valid = sorted(p for p in (root / "valid").glob("*.toml") if not p.name.endswith(".canonical.toml"))
    texts = positioned = 0
    for document in valid:
        expected = json.loads(document.with_suffix(".json").read_bytes().decode("utf-8"))
        for path in (document, document.with_name(document.stem + ".canonical.toml")):
            if path.exists():
                compare_tagged(tomllib.loads(path.read_bytes().decode("utf-8")), expected, path.name)
                texts += 1
        positions = document.with_name(document.stem + ".positions.json")
        if positions.exists():
            text = document.read_bytes().decode("utf-8")
            check_positions(tomllib.loads(text), text, json.loads(positions.read_bytes().decode("utf-8")))
            positioned += 1
    invalid = sorted((root / "invalid").glob("*.toml"))
    rejected = 0
    for path in invalid:
        try:
            tomllib.loads(path.read_bytes().decode("utf-8"))
        except (UnicodeDecodeError, tomllib.TOMLDecodeError, RecursionError):  # 3.14 caps key parts at 1000
            rejected += 1
    assert valid and invalid, "no corpus documents found"
    overlays = check_overlay(root)
    schemas = check_schema(root)
    print(f"tomllib: {len(valid)} valid corpus documents ({texts} texts with their canonical forms) match their "
          f"expected trees, and {positioned} positions files point at their values; {len(invalid)} invalid documents, "
          f"{rejected} of which tomllib rejects too; {overlays} overlays merge to their expected trees and sources; "
          f"{schemas} schema cases read to their values and problems")


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

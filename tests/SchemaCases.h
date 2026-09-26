// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#pragma once

// The schema cases of the corpus (tests/corpus/schema/) as data: a list of fields, read through Reader exactly as an
// application would read its own structs, and the tree of the values it got. tools/make_corpus.cpp writes the cases
// from these structures; tests/CorpusTests.cpp reads them back from JSON and runs them.
#include <felitronics/toml/Schema.h>
#include "Fixtures.h"

namespace schema_cases
{
using namespace felitronics::toml;

struct Field
{
    std::string key;
    std::string type;            // string, integer, decimal, bool, array, table or tables
    std::string of;              // the item type of an array
    bool optional = false;
    std::string min, max;        // inclusive bounds spelled as TOML values, empty for none
    std::vector<Field> fields;   // the fields of a table, or of each table of an array of tables
};
struct Schema
{
    std::vector<Field> fields;
    Severity unknownKeys = Severity::Error;
};

// A bound spelled as a TOML value, read by the library itself.
inline std::optional<Value> bound (const std::string& spelled)
{
    if (spelled.empty()) return std::nullopt;
    auto result = parse ("x = " + spelled);
    if (const auto* t = std::get_if<Table> (&result)) return *t->find ("x");
    return std::nullopt;
}
inline Range<std::int64_t> integers (const Field& f)
{
    Range<std::int64_t> r;
    if (const auto v = bound (f.min); v && std::holds_alternative<std::int64_t> (v->data)) r.min = std::get<std::int64_t> (v->data);
    if (const auto v = bound (f.max); v && std::holds_alternative<std::int64_t> (v->data)) r.max = std::get<std::int64_t> (v->data);
    return r;
}
inline Range<Decimal> decimals (const Field& f)
{
    Range<Decimal> r;
    if (const auto v = bound (f.min)) r.min = asDecimal (*v);
    if (const auto v = bound (f.max)) r.max = asDecimal (*v);
    return r;
}
template <class T> Array items (const std::vector<T>& v)
{
    Array a;
    for (const auto& x : v) a.emplace_back (Value (x));
    return a;
}
inline Array items (const std::vector<bool>& v)
{
    Array a;
    for (const bool x : v) a.emplace_back (Value (x));
    return a;
}

// Reads the fields in order and puts each value it got into out, under the same key.
inline void readFields (Reader& in, const std::vector<Field>& fields, Table& out)
{
    for (const auto& f : fields)
    {
        const auto need = f.optional ? Need::Optional : Need::Required;
        const auto get = [&] (auto& x, const auto& range) { return f.optional ? in.optional (f.key, x, range) : in.required (f.key, x, range); };
        const auto keep = [&] (Value v) { fixtures::put (out, f.key, std::move (v)); };
        const std::string& type = f.type == "array" ? f.of : f.type;
        const bool array = f.type == "array";
        if (type == "string")
        {
            if (! array) { std::string x; if (get (x, detail::Unbounded{})) keep (x); }
            else { std::vector<std::string> x; if (get (x, detail::Unbounded{})) keep (items (x)); }
        }
        else if (type == "bool")
        {
            if (! array) { bool x = false; if (get (x, detail::Unbounded{})) keep (x); }
            else { std::vector<bool> x; if (get (x, detail::Unbounded{})) keep (items (x)); }
        }
        else if (type == "integer")
        {
            if (! array) { std::int64_t x = 0; if (get (x, integers (f))) keep (x); }
            else { std::vector<std::int64_t> x; if (get (x, integers (f))) keep (items (x)); }
        }
        else if (type == "decimal")
        {
            if (! array) { Decimal x; if (get (x, decimals (f))) keep (x); }
            else { std::vector<Decimal> x; if (get (x, decimals (f))) keep (items (x)); }
        }
        else if (f.type == "table")
            (void) in.table (f.key, need, [&] (Reader& sub) { Table t; readFields (sub, f.fields, t); keep (std::move (t)); });
        else if (f.type == "tables")
        {
            Tables rows;
            if (in.tables (f.key, need, [&] (Reader& row) { Table t; readFields (row, f.fields, t); rows.push_back (std::move (t)); }))
                keep (rows.empty() ? Value (Array{}) : Value (std::move (rows)));
        }
    }
}
inline Table run (const Table& document, const Schema& schema, Report& report)
{
    Table out;
    report = read (document, [&] (Reader& in) { readFields (in, schema.fields, out); }, { schema.unknownKeys });
    return out;
}

// A problem as the corpus states it.
struct Expected
{
    Fault fault;
    Severity severity;
    std::string path;
    std::uint32_t line, column;
};
inline std::string spelled (const Expected& e)
{
    return std::string (faultName (e.fault)) + (e.severity == Severity::Error ? " error " : " warning ") + e.path + " "
         + fixtures::decimalInteger (e.line) + ":" + fixtures::decimalInteger (e.column);
}
inline std::vector<Expected> found (const Report& report)
{
    std::vector<Expected> out;
    for (const auto& p : report.problems) out.push_back ({ p.fault, p.severity, p.path, p.position.line, p.position.column });
    return out;
}
inline std::string spelled (const std::vector<Expected>& all)
{
    std::string out;
    for (const auto& e : all) out += spelled (e) + "\n";
    return out;
}
}

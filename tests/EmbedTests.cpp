// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

// Every valid corpus document, embedded at build time by felitronics_toml2cpp, against what parse() makes of the same
// file: each node's type, value, key, style and positions, lookup of every key, and toTable() back to an equal Table
// that writes the same bytes. A few documents are also checked at compile time, since the data is constexpr.
#include <felitronics/toml/Embedded.h>
#include "toml_test.h"
#include "Fixtures.h"
#include "EmbeddedCorpus.h"
#include "corpus/decimal_signed_zero_and_scale.h"
#include "corpus/inline_tables.h"
#include "corpus/integer_int64_edges.h"
#include "corpus/unicode_everywhere.h"
#include <filesystem>
#include <fstream>
#include <iterator>

using namespace felitronics::toml;
namespace fs = std::filesystem;

// Constant expressions: no parser, no allocation.
namespace compiled
{
constexpr auto edges = felitronics_toml_corpus::integer_int64_edges.root();
static_assert (edges.find ("a").integer() == std::numeric_limits<std::int64_t>::min());
static_assert (edges.find ("b").integer() == std::numeric_limits<std::int64_t>::max());
static_assert (edges.find ("b").position().line == 2 && edges.find ("b").position().column == 3);
static_assert (! edges.find ("c") && ! edges.find ("a").find ("x") && ! edges.find ("a").string());
constexpr auto zeros = felitronics_toml_corpus::decimal_signed_zero_and_scale.root().find ("a");
static_assert (zeros.size() == 3 && zeros[0].decimal()->negativeZero && zeros[1].decimal()->scale == 2
               && zeros[2].decimal()->mantissa == -1000 && ! zeros[3]);
constexpr auto inlined = felitronics_toml_corpus::inline_tables.root();
static_assert (inlined.find ("b").isInline() && inlined.find ("b").find ("t").find ("u").boolean() == true);
static_assert (inlined.find ("c").find ("quoted key").integer() == 1 && inlined.find ("c").find ("d").find ("f").decimal()->scale == 2);
static_assert (felitronics_toml_corpus::unicode_everywhere.root().find ("emoji")[2].string()->size() == 8);
constexpr int count (embedded::View v)
{
    int n = 0;
    for (const auto item : v) n += item ? 1 : 0;
    return n;
}
static_assert (count (inlined) == 4 && count (inlined.find ("b")) == 4);
}

namespace
{
std::string number (std::size_t n) { return fixtures::decimalInteger (n); }
bool samePosition (Position a, Position b) { return a.line == b.line && a.column == b.column; }

std::string tableDiffers (embedded::View v, const Table& t, const std::string& at);
std::string differs (embedded::View v, const Value& value, const std::string& at)
{
    if (! samePosition (v.position(), value.position)) return at + ": position";
    if (const auto* s = std::get_if<std::string> (&value.data)) return v.string() == *s ? "" : at + ": string";
    if (const auto* i = std::get_if<std::int64_t> (&value.data)) return v.integer() == *i ? "" : at + ": integer";
    if (const auto* d = std::get_if<Decimal> (&value.data)) return v.decimal() == *d ? "" : at + ": decimal";
    if (const auto* b = std::get_if<bool> (&value.data)) return v.boolean() == *b ? "" : at + ": boolean";
    if (const auto* t = std::get_if<Table> (&value.data)) return tableDiffers (v, *t, at);
    if (const auto* a = std::get_if<Array> (&value.data))
    {
        if (! v.is (embedded::Type::Array) || v.size() != a->size()) return at + ": array";
        for (std::size_t i = 0; i < a->size(); ++i)
            if (auto r = differs (v[i], (*a)[i], at + "[" + number (i) + "]"); ! r.empty()) return r;
        return {};
    }
    const auto& ts = std::get<Tables> (value.data);
    if (! v.is (embedded::Type::Tables) || v.size() != ts.size()) return at + ": array of tables";
    for (std::size_t i = 0; i < ts.size(); ++i)
    {
        const auto where = at + "[" + number (i) + "]";
        if (! samePosition (v[i].position(), ts[i].position)) return where + ": position";
        if (auto r = tableDiffers (v[i], ts[i], where); ! r.empty()) return r;
    }
    return {};
}
std::string tableDiffers (embedded::View v, const Table& t, const std::string& at)
{
    if (! v.is (embedded::Type::Table) || v.size() != t.entries().size()) return at + ": table";
    if (v.isInline() != (t.style == Table::Style::Inline)) return at + ": style";
    for (std::size_t i = 0; i < t.entries().size(); ++i)
    {
        const auto& e = t.entries()[i];
        const auto entry = v[i];
        const auto where = at + "." + e.key;
        if (entry.key() != e.key) return where + ": entry order";
        if (! samePosition (entry.keyPosition(), e.keyPosition)) return where + ": key position";
        const auto found = v.find (e.key);
        if (! found || found.key() != e.key) return where + ": find()";
        if (auto r = differs (entry, e.value, where); ! r.empty()) return r;
    }
    return {};
}
}

int main (int argc, char** argv)
{
    if (argc != 2) { std::printf ("usage: felitronics_toml_embed_tests <corpus directory>\n"); return 2; }
    const fs::path corpus = argv[1];
    test::group ("every valid corpus document, embedded and parsed, node for node");
    const auto documents = embeddedCorpus();
    std::size_t valid = 0;
    for (const auto& entry : fs::directory_iterator (corpus / "valid"))
    {
        const auto name = entry.path().filename().string();
        if (name.size() > 5 && name.substr (name.size() - 5) == ".toml" && name.find (".canonical.") == std::string::npos) ++valid;
    }
    test::ok (documents.size() == valid && valid != 0, "every valid document is embedded: " + number (documents.size()) + " of " + number (valid));
    for (const auto& [name, document] : documents)
    {
        std::ifstream in (corpus / "valid" / (std::string (name) + ".toml"), std::ios::binary);
        const std::string text ((std::istreambuf_iterator<char> (in)), std::istreambuf_iterator<char>());
        const auto result = parse (text, 5);
        const auto* parsed = std::get_if<Table> (&result);
        if (parsed == nullptr) { test::ok (false, std::string (name) + ": refused"); continue; }
        const auto root = document->root();
        test::ok (samePosition (root.position(), parsed->position), std::string (name) + ": the root's position");
        const auto why = tableDiffers (root, *parsed, "root");
        test::ok (why.empty(), std::string (name) + ": " + why);
        const auto copy = embedded::toTable (root, 5);
        test::ok (copy == *parsed && writeChecked (copy) == writeChecked (*parsed), std::string (name) + ": toTable() equals the parse and writes its bytes");
        const auto a = fixtures::locate (copy), b = fixtures::locate (*parsed);
        bool positions = a.size() == b.size();
        for (std::size_t i = 0; positions && i < a.size(); ++i)
            positions = a[i].path == b[i].path && a[i].keyed == b[i].keyed && a[i].at == b[i].at && (! a[i].keyed || a[i].key == b[i].key);
        test::ok (positions, std::string (name) + ": toTable() keeps every position, source included");
    }

    test::group ("empty views, and views out of range");
    const embedded::View none;
    test::ok (! none && ! none.type() && none.size() == 0 && ! none[0] && ! none.find ("a") && none.key().empty()
              && none.position() == Position{} && none.begin() == none.end(), "an empty view gives empty results");
    const embedded::Document empty {};
    test::ok (! empty.root(), "a document without nodes has no root");
    const auto edges = felitronics_toml_corpus::integer_int64_edges.root();
    test::ok (edges.type() == embedded::Type::Table && ! edges[2] && ! edges[0][0] && edges[0].size() == 0, "indexes past the end");
    test::ok (embedded::toTable (none).entries().empty(), "toTable() of an empty view is an empty table");
    return test::report();
}

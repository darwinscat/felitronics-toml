// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#pragma once
#include <felitronics/toml/Toml.h>
#include <bit>
#include <cstdlib>

namespace fixtures
{
using namespace felitronics::toml;
struct Generator
{
    std::uint64_t state;
    std::uint64_t next() { state = state * 6364136223846793005ULL + 1442695040888963407ULL; return state; }
};
inline void put (Table& t, std::string key, Value v)
{
    if (! t.insert (std::move (key), std::move (v))) std::abort();
}
inline std::string decimalInteger (std::uint64_t n)
{
    std::string out;
    do { out.insert (out.begin(), char ('0' + n % 10)); n /= 10; } while (n != 0);
    return out;
}
inline std::string specialString (Generator& g)
{
    std::string s = "\"\\\b\t\n\f\r é Ελληνικά Հայերեն 日本語 😀 ";
    for (int c = 0; c < 32; ++c) s += char (c);
    s += char (127);
    for (unsigned i = 0, n = unsigned (g.next() % 64); i < n; ++i) s += char (32 + g.next() % 95);
    return s;
}
inline Table generated (Generator& g, unsigned depth = 0)
{
    Table t;
    // Deliberately interleave groups: the writer must keep scalar, table and AoT order independently.
    if (depth < 3)
    {
        put (t, "first table", generated (g, depth + 1));
        Tables a;
        for (unsigned i = 0, n = 1 + unsigned (g.next() % 3); i < n; ++i)
        {
            Table e;
            put (e, "label", specialString (g));
            put (e, "enabled", (g.next() & 1) != 0);
            Table settings;
            put (settings, "gain", Decimal { -125, 2 });
            put (e, "settings", std::move (settings));
            Tables children (1);
            put (children[0], "", std::int64_t (i));
            put (e, "children", std::move (children));
            a.push_back (std::move (e));
        }
        put (t, "versions", std::move (a));
        put (t, "second.table", Table{});
    }
    put (t, "", specialString (g));
    put (t, "key\n\"\\é\t", "plain");
    put (t, "minimum", std::numeric_limits<std::int64_t>::min());
    put (t, "maximum", std::numeric_limits<std::int64_t>::max());
    const auto magnitude = std::int64_t (g.next() >> 1);
    put (t, "integer", (g.next() & 1) ? -magnitude : magnitude);
    put (t, "true", true); put (t, "false", false);
    put (t, "empty", Array{});
    put (t, "ints", Array { Value (std::int64_t (0)), Value (std::int64_t (-1)), Value (std::numeric_limits<std::int64_t>::min()) });
    put (t, "bools", Array { Value (true), Value (false) });
    put (t, "strings", Array { Value (specialString (g)), Value (""), Value ("#[],=.") });
    Array decimals;
    for (std::uint8_t scale = 1; scale <= 9; ++scale)
    {
        const auto m = std::int64_t (g.next() % (std::uint64_t (Decimal::kMaxMantissa) + 1));
        put (t, "d" + decimalInteger (scale), Decimal { m, scale });
        for (auto value : { Decimal { m, scale }, Decimal { -m, scale }, Decimal { 0, scale }, Decimal { 0, scale, true },
                           Decimal { Decimal::kMaxMantissa, scale }, Decimal { -Decimal::kMaxMantissa, scale } })
            decimals.emplace_back (value);
    }
    put (t, "decimals", std::move (decimals));
    return t;
}
// This serializer is test-only and independently spells JSON. Decimal expectations carry exact binary64
// bits, so Python checks negative zero and rounding as well as TOML type, tree shape and string contents.
inline std::string jsonString (std::string_view s)
{
    std::string out = "\"";
    for (const char byte : s)
    {
        const auto c = static_cast<unsigned char> (byte);
        if (c == '"' || c == '\\') { out += '\\'; out += char (c); }
        else if (c < 32 || c == 127)
        { out += "\\u00"; out += "0123456789abcdef"[c >> 4]; out += "0123456789abcdef"[c & 15]; }
        else out += char (c);
    }
    return out + '"';
}
inline std::string expectedValue (const Value& value);
inline std::string expectedTable (const Table& table)
{
    std::string out = "[5,[";
    bool first = true;
    for (const auto& e : table.entries())
    {
        if (! first) out += ',';
        first = false;
        out += '[' + jsonString (e.key) + ',' + expectedValue (e.value) + ']';
    }
    return out + "]]";
}
inline std::string expectedValue (const Value& value)
{
    if (const auto* s = std::get_if<std::string> (&value.data)) return "[0," + jsonString (*s) + ']';
    if (const auto* i = std::get_if<std::int64_t> (&value.data))
    {
        const auto magnitude = *i < 0 ? std::uint64_t (-(*i + 1)) + 1 : std::uint64_t (*i);
        return "[1," + std::string (*i < 0 ? "-" : "") + decimalInteger (magnitude) + ']';
    }
    if (const auto* d = std::get_if<Decimal> (&value.data))
    {
        const auto bits = std::bit_cast<std::uint64_t> (d->toDouble());
        std::string hex;
        for (int shift = 60; shift >= 0; shift -= 4) hex += "0123456789abcdef"[(bits >> shift) & 15];
        return "[2,\"" + hex + "\"]";
    }
    if (const auto* b = std::get_if<bool> (&value.data)) return *b ? "[3,true]" : "[3,false]";
    if (const auto* t = std::get_if<Table> (&value.data)) return expectedTable (*t);
    std::string out = std::holds_alternative<Array> (value.data) ? "[4,[" : "[6,[";
    bool first = true;
    if (const auto* a = std::get_if<Array> (&value.data))
        for (const auto& item : *a) { if (! first) out += ','; first = false; out += expectedValue (item); }
    if (const auto* a = std::get_if<Tables> (&value.data))
        for (const auto& item : *a) { if (! first) out += ','; first = false; out += expectedTable (item); }
    return out + "]]";
}
}

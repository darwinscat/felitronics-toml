// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#pragma once
#include <felitronics/toml/Toml.h>
#include <bit>
#include <cstdlib>
#include <utility>
#include <vector>

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
// Cyrillic is spelled with \u escapes in the C++ sources, so a search for Cyrillic prose in them stays meaningful;
// the corpus files hold it raw. The escapes are UTF-8 in every build: MSVC compiles the tests with /utf-8.
inline const std::string ukraina = "\u0423\u043A\u0440\u0430\u0457\u043D\u0430";   // "Ukraina": 7 code points, 14 bytes
inline const std::string klyuch = "\u043A\u043B\u044E\u0447";                       // "klyuch" (key)
inline const std::string kyiv = "\u041A\u0438\u0457\u0432";                         // "Kyiv"

// Latin-1, Greek, Armenian, Cyrillic, CJK, Arabic (right to left), a combining acute, 4-byte emoji, every control.
inline std::string specialString (Generator& g)
{
    std::string s = "\"\\\b\t\n\f\r é Ελληνικά Հայերեն " + ukraina + " 日本語 العربية e\u0301 😀 ";
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
    put (t, klyuch + " 日本語 😀", "a quoted key in three scripts");
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
// Error positions after multi-byte characters. A column counts code points, so each of these would read
// differently in bytes. Every expectation is written by hand.
struct PositionCase { const char* name; std::string text; Code code; std::uint32_t line, column; };
inline std::vector<PositionCase> codePointColumns()
{
    return {
        { "column-after-cyrillic-string", "a = \"" + ukraina + "\" x", Code::TrailingCharacters, 1, 15 },
        { "column-after-cjk-key", "\"日本語\" = 1 x", Code::TrailingCharacters, 1, 11 },
        { "column-after-emoji", "a = \"😀\\q\"", Code::InvalidEscape, 1, 8 },
        { "column-after-combining-mark", "a = \"e\u0301\" x", Code::TrailingCharacters, 1, 10 },
        { "column-after-arabic-key", "\"العربية\" = [1, true]", Code::MixedArray, 1, 17 },
        { "column-after-cyrillic-comment", "# " + ukraina + " \x80", Code::InvalidUtf8, 1, 11 },
        { "column-at-end-after-cjk", "a = \"日本", Code::UnterminatedString, 1, 8 },
        { "column-after-cyrillic-dotted-key", "\"" + klyuch + "\".a = 1\n\"" + klyuch + "\".a.b = 2", Code::TableValueConflict, 2, 8 },
        { "column-after-cyrillic-header", "[\"" + kyiv + "\".a]\n[\"" + kyiv + "\".a]", Code::RedefinedTable, 2, 9 },
        { "column-after-tab-and-multibyte", "\ta = \"é\"\tx", Code::TrailingCharacters, 1, 10 },
        // A limit crossed by a multi-byte character points at that character, not at the byte that crossed it.
        { "limit-quoted-key-two-byte-character", "\"" + std::string (kMaxKey - 1, 'a') + "é\"=0", Code::KeyLimit, 1, 257 },
        { "limit-quoted-key-four-byte-character", "\"" + std::string (kMaxKey - 3, 'a') + "😀\"=0", Code::KeyLimit, 1, 255 },
        { "limit-string-two-byte-character", "a=\"" + std::string (kMaxString - 1, 'x') + "é\"", Code::StringLimit, 1, 65539 } };
}
// Accepted documents that use non-ASCII text in every place TOML allows it, and limits met exactly by multi-byte text.
inline std::vector<std::pair<const char*, std::string>> unicodeDocuments()
{
    const std::string znachennya = "\u0437\u043D\u0430\u0447\u0435\u043D\u043D\u044F"; // "znachennya" (value)
    const std::string tablytsya = "\u0442\u0430\u0431\u043B\u0438\u0446\u044F";         // "tablytsya" (table)
    return {
        { "unicode-everywhere",
          "# " + ukraina + ", 日本語, العربية, e\u0301, 😀\n"
          "\"" + klyuch + "\" = \"" + znachennya + "\"  # " + kyiv + "\n"
          "\"日本語\" = \"テキスト\"\n"
          "\"العربية\" = \"نص\"\n"
          "combining = \"e\u0301\"\n"
          "emoji = [\"😀\", \"👍🏽\", \"🇺🇦\"]\n"
          "\n"
          "[\"" + tablytsya + "\"]\n"
          "\"😀\" = true\n"
          "\n"
          "[[\"" + kyiv + "\"]]\n"
          "name = \"" + kyiv + "\"\n" },
        { "limit-quoted-key-two-byte-character", "\"" + std::string (kMaxKey - 2, 'a') + "é\"=0" },
        { "limit-quoted-key-four-byte-character", "\"" + std::string (kMaxKey - 4, 'a') + "😀\"=0" },
        { "limit-string-two-byte-character", "a=\"" + std::string (kMaxString - 2, 'x') + "é\"" } };
}
// Every position in a tree, depth first in entry order: the root, then each entry (its key and its value), each
// array item and each array-of-tables element. The path is a JSON array of keys and item indexes.
struct Located { std::string path; bool keyed; Position key, at; };
inline std::string jsonString (std::string_view s);
inline void locate (std::vector<Located>& out, const Table& table, const std::string& path)
{
    const auto join = [] (const std::string& p, const std::string& component)
    { return p.size() == 2 ? "[" + component + "]" : p.substr (0, p.size() - 1) + ", " + component + "]"; };
    for (const auto& e : table.entries())
    {
        const auto at = join (path, jsonString (e.key));
        out.push_back ({ at, true, e.keyPosition, e.value.position });
        if (const auto* a = std::get_if<Array> (&e.value.data))
            for (std::size_t i = 0; i < a->size(); ++i) out.push_back ({ join (at, decimalInteger (i)), false, {}, (*a)[i].position });
        else if (const auto* t = std::get_if<Table> (&e.value.data)) locate (out, *t, at);
        else if (const auto* ts = std::get_if<Tables> (&e.value.data))
            for (std::size_t i = 0; i < ts->size(); ++i)
            {
                const auto element = join (at, decimalInteger (i));
                out.push_back ({ element, false, {}, (*ts)[i].position });
                locate (out, (*ts)[i], element);
            }
    }
}
inline std::vector<Located> locate (const Table& root)
{
    std::vector<Located> out { { "[]", false, {}, root.position } };
    locate (out, root, "[]");
    return out;
}
inline std::string positionsJson (const Table& root)
{
    const auto pair = [] (Position p) { return "[" + decimalInteger (p.line) + ", " + decimalInteger (p.column) + "]"; };
    std::string out = "[\n";
    const auto all = locate (root);
    for (std::size_t i = 0; i < all.size(); ++i)
        out += "  {\"path\": " + all[i].path + (all[i].keyed ? ", \"key\": " + pair (all[i].key) : "") + ", \"at\": "
             + pair (all[i].at) + (i + 1 == all.size() ? "}\n" : "},\n");
    return out + "]\n";
}
// The byte offset of a position, found without the parser: count lines, then code points within the line.
inline std::size_t offsetOf (std::string_view text, Position p)
{
    std::size_t i = 0;
    for (std::uint32_t line = 1; line < p.line && i < text.size(); ++i)
        if (text[i] == '\n') ++line;
    for (std::uint32_t column = 1; i < text.size(); ++i)
        if ((static_cast<unsigned char> (text[i]) & 0xC0u) != 0x80u && column++ == p.column) break;
    return i;
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

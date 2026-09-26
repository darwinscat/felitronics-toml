// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#include <felitronics/toml/Toml.h>
#include "toml_test.h"

using namespace felitronics::toml;

namespace
{
std::string number (std::size_t n)
{
    std::string s;
    do { s.insert (s.begin(), char ('0' + n % 10)); n /= 10; } while (n);
    return s;
}
void accepted (std::string_view text)
{
    const auto result = parse (text);
    const auto* root = std::get_if<Table> (&result);
    if (! root)
    {
        const auto e = std::get<Error> (result);
        test::ok (false, std::string (codeName (e.code)) + " at " + number (e.line) + ":" + number (e.column));
        return;
    }
    const auto canonical = write (*root);
    const auto next = parse (canonical);
    const auto* tree = std::get_if<Table> (&next);
    test::ok (tree != nullptr && *root == *tree && write (*tree) == canonical, "accepted and canonical round trip");
}
void checkError (std::string_view text, Code code, std::uint32_t line, std::uint32_t column)
{
    const auto result = parse (text);
    const auto* e = std::get_if<Error> (&result);
    test::ok (e && *e == Error { code, line, column }, std::string (codeName (code)) + " at " + number (line) + ":" + number (column)
        + (e ? " (got " + std::string (codeName (e->code)) + " " + number (e->line) + ":" + number (e->column) + ")" : " (accepted)"));
}
std::string path (std::size_t n)
{
    std::string out = "a";
    for (std::size_t i = 1; i < n; ++i) out += ".a";
    return out;
}
// A fixture step that must succeed. It is not an assertion about the parser, so it only counts when it fails.
void setUp (bool accepted)
{
    if (! accepted) test::ok (false, "fixture insertion refused");
}
}
int main()
{
    bool seen[std::size_t (Code::EntryLimit) + 1] {};
    const auto error = [&] (std::string_view text, Code code, std::uint32_t line, std::uint32_t column)
    {
        seen[std::size_t (code)] = true;
        checkError (text, code, line, column);
    };
    test::group ("minimal reachable error codes, one-based byte coordinates, first error only");
    error ("\xEF\xBB\xBF", Code::Bom, 1, 1);
    error ("\x80", Code::InvalidUtf8, 1, 1);
    error (std::string (1, '\0'), Code::InvalidControl, 1, 1);
    error ("\r", Code::BareCarriageReturn, 1, 1);
    error ("=", Code::ExpectedKey, 1, 1);
    error ("a", Code::ExpectedEquals, 1, 2);
    error ("a=", Code::ExpectedValue, 1, 3);
    error ("a='x'", Code::UnsupportedValue, 1, 3);
    error ("a=\"", Code::UnterminatedString, 1, 4);
    error ("a=\"\\z\"", Code::InvalidEscape, 1, 5);
    error ("a=\"\\uX\"", Code::InvalidUnicodeEscape, 1, 6);
    error ("a=00", Code::InvalidNumber, 1, 3);
    error ("a=9223372036854775808", Code::IntegerRange, 1, 3);
    error ("a=0.0000000000", Code::DecimalScale, 1, 14);
    error ("a=900719925474099.3", Code::DecimalRange, 1, 3);
    error ("a=[0 0]", Code::ExpectedArraySeparator, 1, 6);
    error ("a=[", Code::UnterminatedArray, 1, 4);
    error ("a=[0,true]", Code::MixedArray, 1, 6);
    error ("[a", Code::ExpectedHeaderEnd, 1, 3);
    error ("a=0 x", Code::TrailingCharacters, 1, 5);
    error ("a=0\na=1", Code::DuplicateKey, 2, 1);
    error ("[a]\n[a]", Code::RedefinedTable, 2, 2);
    error ("a=0\n[a]", Code::TableValueConflict, 2, 2);
    error ("# é\r\n\t a = \"x\\q\"", Code::InvalidEscape, 2, 10);
    error ("=\n\x80", Code::ExpectedKey, 1, 1);
    error ("a=\"\x80\"\na=1", Code::InvalidUtf8, 1, 4);
    error ("a=\"é\" x", Code::TrailingCharacters, 1, 8);

    test::group ("supported grammar and TOML table ownership");
    for (const char* text : { "", " \t# hi", "\r\n#c\r\na=+0\r\n", "\"\"=true", "123=42", "a=\"\"",
            "a=[ # hi\n1, # next\r\n2,\n] #done", "a=[]", "a=\"raw\ttab\"", "a=\"\\u0000\\u007F\\U0010FFFF\"",
            "a . \"b.c\" = false", "[ a . \"b.c\" ]\nx=1", "[[ a . b ]]\nx=1\n[[a.b]]\nx=2",
            "[a.b]\nx=1\n[a]\ny=2", "a.b.c=1\na.b.d=2", "a.b=1\n[a.c]\nx=2",
            "[[a]]\nx=1\n[a.b]\ny=2\n[[a]]\nx=3\n[a.b]\ny=4",
            "[[a]]\n[[a.b]]\nx=1\n[[a.b]]\nx=2\n[[a]]\n[[a.b]]\nx=3",
            "a=-9223372036854775808\nb=9223372036854775807", "a=900719925474099.2\nb=-9007199.254740992",
            "a=[-0.0,0.00,-1.000]", "a=[true,false]", "a=[\"é\",\"😀\"]", "[a]\n[b]\n" }) accepted (text);
    error ("a.b=1\n[a]", Code::RedefinedTable, 2, 2);
    error ("[a.b]\n[a]\nb.c=1", Code::RedefinedTable, 3, 1);
    error ("[a]\n[a.b]\n[a]", Code::RedefinedTable, 3, 2);
    error ("a.b=1\na=2", Code::TableValueConflict, 2, 1);
    error ("a=1\na.b=2", Code::TableValueConflict, 2, 1);
    error ("[a]\n[[a]]", Code::TableValueConflict, 2, 3);
    error ("[[a]]\n[a]", Code::TableValueConflict, 2, 2);
    error ("a=[]\n[[a]]", Code::TableValueConflict, 2, 3);
    error ("[a]\nx=1\nx=2", Code::DuplicateKey, 3, 1);
    error ("a=1\n\"a\"=2", Code::DuplicateKey, 2, 1);
    for (const char* value : { "01", "-01", "+01", "01.1", "1.", ".1", "1e2", "1.0e2", "1_000", "0xff",
                               "0o77", "0b01", "+", "-", "True", "falsee", "inf", "nan", "1979-05-27", "12:00:00" })
    {
        const auto result = parse (std::string ("a=") + value);
        test::ok (std::holds_alternative<Error> (result), "unsupported number or date refused");
    }
    error ("a=[[0]]", Code::UnsupportedValue, 1, 4);
    error ("a={}", Code::UnsupportedValue, 1, 3);
    error ("a=\"\"\"x\"\"\"", Code::TrailingCharacters, 1, 5);
    error ("a=\"x\ny\"", Code::UnterminatedString, 1, 5);
    error ("a=\"\\uD800\"", Code::InvalidUnicodeEscape, 1, 4);
    error ("a=\"\\U00110000\"", Code::InvalidUnicodeEscape, 1, 4);
    error ("a=\"\\UFFFFFFFF\"", Code::InvalidUnicodeEscape, 1, 4);
    error ("a=\"\\", Code::UnterminatedString, 1, 5);
    error ("a=\"\\u12", Code::InvalidUnicodeEscape, 1, 8);
    error ("a=[1,", Code::UnterminatedArray, 1, 6);
    error ("a=[,]", Code::ExpectedValue, 1, 4);
    error ("a=[1,,]", Code::ExpectedValue, 1, 6);
    error ("[[a]", Code::ExpectedHeaderEnd, 1, 5);

    test::group ("each resource at the limit and at limit + 1");
    accepted (std::string (kMaxDocument, ' '));
    error (std::string (kMaxDocument + 1, ' '), Code::DocumentLimit, 1, std::uint32_t (kMaxDocument + 1));
    accepted (path (kMaxDepth) + "=0");
    error (path (kMaxDepth + 1) + "=0", Code::DepthLimit, 1, 33);
    accepted ("[" + path (kMaxDepth) + "]");
    error ("[" + path (kMaxDepth + 1) + "]", Code::DepthLimit, 1, 34);
    error ("[" + path (kMaxDepth) + "]\nx=0", Code::DepthLimit, 2, 1);
    accepted (std::string (kMaxKey, 'a') + "=0");
    error (std::string (kMaxKey + 1, 'a') + "=0", Code::KeyLimit, 1, 257);
    accepted ("\"" + std::string (kMaxKey, 'a') + "\"=0");
    error ("\"" + std::string (kMaxKey + 1, 'a') + "\"=0", Code::KeyLimit, 1, 258);
    accepted ("a=\"" + std::string (kMaxString, 'x') + "\"");
    error ("a=\"" + std::string (kMaxString + 1, 'x') + "\"", Code::StringLimit, 1, 65540);
    std::string a = "a=[";
    for (std::size_t i = 0; i < kMaxArray; ++i) a += "0,";
    accepted (a + "]");
    error (a + "0]", Code::ArrayLimit, 1, 131076);
    std::string entries;
    for (std::size_t i = 0; i < kMaxEntries; ++i) entries += "a" + number (i) + "=0\n";
    accepted (entries);
    error (entries + "x=0", Code::EntryLimit, 65537, 1);
    // The AoT container is one entry; every table element is another. Implicit tables count, too.
    std::string tables;
    for (std::size_t i = 0; i < kMaxEntries - 1; ++i) tables += "[[a]]\n";
    accepted (tables);
    error (tables + "[[a]]", Code::EntryLimit, 65536, 3);
    // Escaped control bytes are legal data. Their canonical representation fits exactly at this size.
    Table full;
    for (int i = 0; i < 15; ++i) setUp (full.insert ("s" + number (std::size_t (i)), std::string (65536, 'x')));
    const std::size_t used = write (full).size();
    const auto tail = kMaxDocument - used - 10; // tail = "..."\n costs ten bytes
    setUp (full.insert ("tail", std::string (tail, 'x')));
    auto exact = writeChecked (full);
    test::ok (exact && exact->size() == kMaxDocument, "canonical document exactly at the byte limit");
    if (exact)
    {
        accepted (*exact);
        // Remove optional whitespace and add one string byte: input fits, canonical form does not.
        auto compact = *exact;
        const auto space = compact.find (" = ");
        compact.erase (space + 2, 1);
        compact.erase (space, 1);
        compact.insert (compact.size() - 2, "x");
        error (compact, Code::CanonicalLimit, 17, 1);
    }

    test::group ("hostile bytes, including every context and UTF-8 encoding boundary");
    error (std::string (kMaxDocument, '['), Code::ExpectedKey, 1, 3);
    error (path (100000), Code::DepthLimit, 1, 33);
    a = "a=[";
    for (int i = 0; i < 100000; ++i) a += "0,";
    error (a + "]", Code::ArrayLimit, 1, 131076);
    error ("a=\"" + std::string (900000, 'x'), Code::StringLimit, 1, 65540);
    for (const auto& invalid : { std::string ("\x80"), std::string ("\xC0\xAF"), std::string ("\xC1\xBF"),
            std::string ("\xC2"), std::string ("\xE0\x80\x80"), std::string ("\xED\xA0\x80"),
            std::string ("\xF0\x80\x80\x80"), std::string ("\xF4\x90\x80\x80"), std::string ("\xF5\x80\x80\x80"),
            std::string ("\xFE"), std::string ("\xFF"), std::string ("\xE2\x82"), std::string ("\xE2x\xA0") })
    {
        error ("#" + invalid, Code::InvalidUtf8, 1, 2);
        error ("\"" + invalid + "\"=0", Code::InvalidUtf8, 1, 2);
        error ("a=\"" + invalid + "\"", Code::InvalidUtf8, 1, 4);
        error ("[\"" + invalid + "\"]", Code::InvalidUtf8, 1, 3);
        error ("a=[\"" + invalid + "\"]", Code::InvalidUtf8, 1, 5);
    }
    for (int b = 0; b < 128; ++b)
        if ((b < 32 && b != 9 && b != 10 && b != 13) || b == 127)
        {
            const std::string raw (1, char (b));
            error ("#" + raw, Code::InvalidControl, 1, 2);
            error ("a=\"" + raw + "\"", Code::InvalidControl, 1, 4);
            error ("\"" + raw + "\"=1", Code::InvalidControl, 1, 2);
        }
    accepted ("a=\"\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xF0\x90\x80\x80\xF4\x8F\xBF\xBF\"");
    error ("# hi\r", Code::BareCarriageReturn, 1, 5);
    error ("a=\"x\r\"", Code::BareCarriageReturn, 1, 5);
    const std::string allPositions = "# hi\n\"name\"=\"text\"\n[a.b]\nx=[true,false]\n[[c]]\nx=-1.25\n";
    for (std::size_t i = 0; i <= allPositions.size(); ++i)
    {
        std::uint32_t line = 1, column = 1;
        for (std::size_t j = 0; j < i; ++j)
            if (allPositions[j] == '\n') { ++line; column = 1; } else ++column;
        for (const char byte : { '\x80', '\0' })
        {
            auto changed = allPositions;
            changed.insert (i, 1, byte);
            error (changed, byte == 0 ? Code::InvalidControl : Code::InvalidUtf8, line, column);
        }
    }
    for (std::size_t i = 0; i < sizeof (seen) / sizeof (seen[0]); ++i)
        test::ok (seen[i], std::string (codeName (Code (i))) + " has an exact-position rejection witness");
    test::ok (std::string_view (codeName (Code (999))) == "Unknown", "logging an unknown enum is bounded");
    return test::report();
}

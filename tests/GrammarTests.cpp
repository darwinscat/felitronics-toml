// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#include <felitronics/toml/Toml.h>
#include "toml_test.h"
#include "Fixtures.h"

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
    bool seen[std::size_t (Code::ExpectedInlineTableSeparator) + 1] {};
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
    error ("a=\"é\" x", Code::TrailingCharacters, 1, 7);
    // A bare value is one token: running into an invalid byte gives that byte's error, not the digits' own.
    error ("a=0.0000000000\x80", Code::InvalidUtf8, 1, 15);

    test::group ("columns count code points: every script, a combining mark, a tab, limits crossed by multi-byte text");
    for (const auto& c : fixtures::codePointColumns()) error (c.text, c.code, c.line, c.column);
    for (const auto& [name, text] : fixtures::unicodeDocuments()) accepted (text);

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
    for (const char* value : { "01", "-01", "+01", "01.1", "1.", ".1", "1e2", "1.0e2", "0xff",
                               "0o77", "0b01", "+", "-", "True", "falsee", "inf", "nan", "1979-05-27", "12:00:00" })
    {
        const auto result = parse (std::string ("a=") + value);
        test::ok (std::holds_alternative<Error> (result), "unsupported number or date refused");
    }
    error ("a=[[0]]", Code::UnsupportedValue, 1, 4);
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

    test::group ("inline tables: one line, no trailing comma, closed once defined, nested within the depth limit");
    for (const char* text : { "a = {}", "a={x=1}", "a = { x = 1, y = \"s\", z = [1, 2], t = { u = true } }",
            "a = { \"quoted key\" = 1, b.c = 2, b.d = 3 }", "a = { x = 1 } # a comment", "[t]\na = { x = { y = { z = 1 } } }",
            "a = { s = \"\"\"line\nline\"\"\" }", "a = { b = [\n1, # a comment\n2,\n] }", "a = [{ x = 1 }, { x = 2 }]",
            "a = [\n  { x = 1 },\n  { x = 2 }, # a comment\n]", "a = [{}, {}]", "a = [{ b = [{ c = 1 }] }]",
            "[[a]]\nb = { c = 1 }\n[[a]]\nb = [{ c = 2 }]", "a.b = { c = 1 }\na.d = 2", "a = {x=1}\nb = {x=1}" })
        accepted (text);
    error ("a = { x = 1, }", Code::ExpectedKey, 1, 14);
    error ("a = { , }", Code::ExpectedKey, 1, 7);
    error ("a = { x = 1\n}", Code::UnterminatedInlineTable, 1, 12);
    error ("a = { x = 1,\ny = 2 }", Code::UnterminatedInlineTable, 1, 13);
    error ("a = {\n}", Code::UnterminatedInlineTable, 1, 6);
    error ("a = {", Code::UnterminatedInlineTable, 1, 6);
    error ("a = { # c\n}", Code::UnterminatedInlineTable, 1, 7);
    error ("a = { x = 1 # c\n}", Code::UnterminatedInlineTable, 1, 13);
    error ("a={x\n}", Code::UnterminatedInlineTable, 1, 5);
    error ("a={x=\n1}", Code::UnterminatedInlineTable, 1, 6);
    error ("a={x", Code::UnterminatedInlineTable, 1, 5);
    error ("a={x=", Code::UnterminatedInlineTable, 1, 6);
    error ("a={x = # c\n1}", Code::UnterminatedInlineTable, 1, 8);
    error ("a={b.\nc=1}", Code::UnterminatedInlineTable, 1, 6);
    error ("a={b.c\n=1}", Code::UnterminatedInlineTable, 1, 7);
    error ("a = { x = 1 y = 2 }", Code::ExpectedInlineTableSeparator, 1, 13);
    error ("a = { x = 1 ]", Code::ExpectedInlineTableSeparator, 1, 13);
    error ("a = { x = }", Code::ExpectedValue, 1, 11);
    error ("a = { x }", Code::ExpectedEquals, 1, 9);
    error ("a = { x = 1 }}", Code::TrailingCharacters, 1, 14);
    error ("a = {x=1}x", Code::TrailingCharacters, 1, 10);
    error ("a = { x = 1, x = 2 }", Code::DuplicateKey, 1, 14);
    error ("a = { x.y = 1, x = 2 }", Code::TableValueConflict, 1, 16);
    error ("a = { x = { z = 1 }, x.y = 2 }", Code::TableValueConflict, 1, 22);
    // An inline table is a value: headers and dotted keys cannot open it again, and assigning it again is a duplicate.
    error ("a = {}\na.b = 1", Code::TableValueConflict, 2, 1);
    error ("a = {}\n[a]", Code::TableValueConflict, 2, 2);
    error ("a = { x = 1 }\n[a.y]", Code::TableValueConflict, 2, 2);
    error ("a = { x = 1 }\n[[a.y]]", Code::TableValueConflict, 2, 3);
    error ("a = [{}]\n[[a]]", Code::TableValueConflict, 2, 3);
    error ("a = [{}]\n[a.b]", Code::TableValueConflict, 2, 2);
    error ("a = {}\na = 1", Code::DuplicateKey, 2, 1);
    error ("a = [{}]\na = {}", Code::DuplicateKey, 2, 1);
    error ("a.b = 1\na = {}", Code::TableValueConflict, 2, 1);
    error ("[a]\n[b]\n[a.c]\n[b.d]\n[a]", Code::RedefinedTable, 5, 2);
    // One type per array: scalars of one alternative, or inline tables. Nested arrays stay refused.
    error ("a = [1, {}]", Code::MixedArray, 1, 9);
    error ("a = [{}, 1]", Code::MixedArray, 1, 10);
    error ("a = [{}, [1]]", Code::UnsupportedValue, 1, 10);
    error ("a = [{ x = 01 }, 1]", Code::InvalidNumber, 1, 12);
    error ("a = [{} {}]", Code::ExpectedArraySeparator, 1, 9);
    error ("a = [{}", Code::UnterminatedArray, 1, 8);
    // Depth: an inline table's keys continue its key's path.
    accepted (path (kMaxDepth - 1) + " = { x = 1 }");
    error (path (kMaxDepth) + " = { x = 1 }", Code::DepthLimit, 1, 37);
    // An element's keys are one level below its array's key, as under [[a.b]].
    accepted ("[" + path (kMaxDepth - 2) + "]\nb = [{ c = 1 }]");
    error ("[" + path (kMaxDepth - 1) + "]\nb = [{ c = 1 }]", Code::DepthLimit, 2, 8);
    error ("[" + path (kMaxDepth - 2) + "]\nb = [{ c = { d = 1 } }]", Code::DepthLimit, 2, 14);
    {
        std::string nested = "a = ", close;
        for (std::size_t i = 1; i < kMaxDepth; ++i) { nested += "{ a = "; close += " }"; }
        accepted (nested + "1" + close);
        error (nested + "{ a = 1 }" + close, Code::DepthLimit, 1, 97);
    }
    {
        // Arrays of inline tables inside each other: each level one key deeper, to the limit and past it.
        std::string open, close;
        for (std::size_t i = 1; i < kMaxDepth; ++i) { open += "a = [{ "; close += " }]"; }
        accepted (open + "x = 1" + close);
        error (open + "a = [{ x = 1 }]" + close, Code::DepthLimit, 1, 113);
    }
    // Entries: every key and every element counts once, when it is inserted; an array's key after its elements.
    {
        std::string rows = "a = [";
        for (std::size_t i = 0; i + 1 < kMaxEntries; ++i) rows += "{},";
        accepted (rows + "]");
        error (rows + "{}]", Code::EntryLimit, 1, 1);
        std::string wide = "a = [";
        for (std::size_t i = 0; i < kMaxArray; ++i) wide += "0,";
        error ("x = [" + std::string (wide.substr (5)) + "{}]", Code::ArrayLimit, 1, std::uint32_t (6 + 2 * kMaxArray));
    }
    {
        const auto result = parse ("x = 1\nt = { b = 2, c = { d = 3 } }\nrows = [{ e = 4 }, { e = 5 }]\n[h]\ni = { j = 6 }\n");
        const auto* root = std::get_if<Table> (&result);
        const auto* t = root ? std::get_if<Table> (&root->find ("t")->data) : nullptr;
        const auto* rows = root ? std::get_if<Tables> (&root->find ("rows")->data) : nullptr;
        test::ok (t && t->style == Table::Style::Inline && std::get_if<Table> (&t->find ("c")->data)->style == Table::Style::Inline,
                  "the parser marks tables read in braces inline");
        test::ok (rows && rows->size() == 2 && (*rows)[1].style == Table::Style::Inline && root->find ("rows")->position == Position { 3, 8, 0 }
                  && (*rows)[1].position == Position { 3, 20, 0 }, "an array of inline tables is a Tables value, each element at its brace");
        test::ok (root && write (*root) == "x = 1\nt = { b = 2, c = { d = 3 } }\nrows = [\n    { e = 4 },\n    { e = 5 },\n]\n\n[h]\ni = { j = 6 }\n",
                  "write() keeps inline tables inline, one array element per line");
        // The same data with other styles is equal, and writes as the styles say.
        auto headers = parse ("x = 1\nrows = [{ e = 4 }, { e = 5 }]\n[t]\nb = 2\n[t.c]\nd = 3\n[h.i]\nj = 6\n");
        test::ok (root && std::holds_alternative<Table> (headers) && std::get<Table> (headers) == *root, "styles are not part of equality");
        Table built;
        Table inner;
        setUp (inner.insert ("k", Value (std::int64_t (1))));
        Table header = inner;
        inner.style = Table::Style::Inline;
        setUp (built.insert ("inline", Value (inner)));
        setUp (built.insert ("header", Value (header)));
        Tables mixed { inner, header }, allInline { inner, inner };
        setUp (built.insert ("mixed", Value (mixed)));
        setUp (built.insert ("rows", Value (allInline)));
        Table outer;
        setUp (outer.insert ("header inside", Value (header)));
        setUp (outer.insert ("tables inside", Value (Tables { header })));
        outer.style = Table::Style::Inline;
        setUp (built.insert ("outer", Value (outer)));
        test::ok (write (built) == "inline = { k = 1 }\nrows = [\n    { k = 1 },\n    { k = 1 },\n]\n"
                                   "outer = { \"header inside\" = { k = 1 }, \"tables inside\" = [{ k = 1 }] }\n"
                                   "\n[header]\nk = 1\n\n[[mixed]]\nk = 1\n\n[[mixed]]\nk = 1\n",
                  "a caller sets the style; an array of tables is inline only when every table in it is");
        test::ok (! writeChecked (Table { built }).value_or ("").empty(), "the built tree is writable");
    }

    test::group ("multi-line basic strings: the first line ending dropped, CRLF as LF, line-ending backslashes, quotes");
    {
        const auto read = [] (std::string_view document, const std::string& expected, const char* what)
        {
            const auto result = parse (document);
            const auto* root = std::get_if<Table> (&result);
            const auto* s = root && root->find ("a") ? std::get_if<std::string> (&root->find ("a")->data) : nullptr;
            test::ok (s && *s == expected, what);
            if (root) accepted (document);
        };
        read ("a = \"\"\"\nline one\nline two\"\"\"", "line one\nline two", "the line ending after the quotes is dropped");
        read ("a = \"\"\"\n\nx\"\"\"", "\nx", "only the first one");
        read ("a = \"\"\"one line\"\"\"", "one line", "on one line");
        read ("a = \"\"\"\r\nx\r\ny\r\n\"\"\"", "x\ny\n", "CRLF reads as LF, so a checkout's line endings cannot change a value");
        read ("a = \"\"\"one \\\n    two\"\"\"", "one two", "a line-ending backslash drops the line ending and the indent");
        read ("a = \"\"\"a\\ \t\r\n\n \t\n  b\"\"\"", "ab", "spaces after the backslash, blank lines and CRLF too");
        read ("a = \"\"\"\\\n\"\"\"", "", "up to the closing quotes");
        read ("a = \"\"\"a\"b\"\"c\"\"\"", "a\"b\"\"c", "one or two quotes inside");
        read ("a = \"\"\"x\"\"\"\"", "x\"", "a quote right before the closing three");
        read ("a = \"\"\"x\"\"\"\"\"", "x\"\"", "two quotes right before the closing three");
        read ("a = \"\"\"\"\"\"", "", "empty");
        read ("a = \"\"\"\"\"\"\"", "\"", "one quote");
        read ("a = \"\"\"\\t\\u00E9\t#\\\\\"\"\"", "\t\u00E9\t#\\", "escapes, a raw tab, a hash");
        read ("a = \"\"\"x\ny\"\"\" # a comment", "x\ny", "a comment after the closing quotes");
        read ("a = \"\"\"" + std::string (kMaxString - 2, 'x') + "\"\"\"\"\"", std::string (kMaxString - 2, 'x') + "\"\"",
              "the quotes before the closing three meet the limit");
        accepted ("a = [\"\"\"x\ny\"\"\", \"z\"]\nb = \"\"\"\n\"\"\"");
        // Written back: a statement's string with a line feed on several lines, everything else on one.
        Table t;
        setUp (t.insert ("text", Value ("say \"hi\"\n\"\"x\"\ttab\\\r\n")));
        setUp (t.insert ("list", Value (Array { Value ("a\nb") })));
        setUp (t.insert ("plain", Value ("no line feed")));
        test::ok (write (t) == "text = \"\"\"\nsay \"hi\"\n\\\"\"x\"\ttab\\\\\\r\n\"\"\"\nlist = [\"a\\nb\"]\nplain = \"no line feed\"\n",
                  "the multi-line spelling, and the one-line spelling in an array");
    }
    error ("a = \"\"\"x", Code::UnterminatedString, 1, 9);
    error ("a = \"\"\"x\n", Code::UnterminatedString, 2, 1);
    error ("a = \"\"\"x\"\"\"\"\"\"", Code::TrailingCharacters, 1, 14);
    error ("a = \"\"\"a\\ b\"\"\"", Code::InvalidEscape, 1, 10);
    error ("a = \"\"\"a\\  ", Code::InvalidEscape, 1, 10);
    error ("a = \"\"\"\\q\"\"\"", Code::InvalidEscape, 1, 9);
    error ("a = \"\"\"x\ry\"\"\"", Code::BareCarriageReturn, 1, 9);
    error ("a = \"\"\"\n\x01\"\"\"", Code::InvalidControl, 2, 1);
    error ("\"\"\"a\"\"\" = 1", Code::ExpectedEquals, 1, 3);
    error ("a = \"\"\"" + std::string (kMaxString - 1, 'x') + "\"\"\"\"\"", Code::StringLimit, 1, std::uint32_t (8 + kMaxString));

    test::group ("underscores: each one between two digits, gone from the value and from the canonical text");
    {
        const auto result = parse ("a = 48_000\nb = 1_000.000_1\nc = -9_223_372_036_854_775_808\nd = 9_007_199.254_740_992\ne = +1_0");
        const auto* root = std::get_if<Table> (&result);
        test::ok (root && std::get<std::int64_t> (root->find ("a")->data) == 48000, "48_000");
        test::ok (root && std::get<Decimal> (root->find ("b")->data) == Decimal { 10000001, 4 }, "1_000.000_1");
        test::ok (root && std::get<std::int64_t> (root->find ("c")->data) == std::numeric_limits<std::int64_t>::min(), "int64 minimum");
        test::ok (root && std::get<Decimal> (root->find ("d")->data) == Decimal { Decimal::kMaxMantissa, 9 }, "the largest mantissa");
        test::ok (root && write (*root) == "a = 48000\nb = 1000.0001\nc = -9223372036854775808\nd = 9007199.254740992\ne = 10\n",
                  "the writer spells no underscores");
    }
    for (const char* value : { "1__000", "1000_", "+_1", "-_1", "1_.5", "1._5", "1.5_", "0_1", "0_0.1", "1_e2", "1_000_" })
        error (std::string ("a=") + value, Code::InvalidNumber, 1, 3);
    error ("a=_1", Code::UnsupportedValue, 1, 3);
    error ("a=0.000_000_000_1", Code::DecimalScale, 1, 17);
    error ("a=9_223_372_036_854_775_808", Code::IntegerRange, 1, 3);
    error ("a=900_719_925_474_099.3", Code::DecimalRange, 1, 3);
    error ("a=[1_0, 2__0]", Code::InvalidNumber, 1, 9);

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

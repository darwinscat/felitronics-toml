// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

// Seeds the language-neutral conformance corpus, tests/corpus/ (its README states the format). Run from the root:
//   cmake --build build --target felitronics_toml_make_corpus
//   build/tests/felitronics_toml_make_corpus tests/corpus
//
// Where the expectations come from:
//   invalid/  the code, line and column of every case are written HERE by hand: the same values
//             tests/GrammarTests.cpp asserts, or computed from the text the way it computes them. The
//             generator refuses to write a case the parser disagrees with, so it can seed but never "fix".
//   valid/    the tree and the canonical text are the output of this implementation. They are checked
//             independently by Python's tomllib: tools/python-roundtrip.py --corpus tests/corpus.
// Once written, the files are the contract. Add a case by adding files; this program only reseeds.
#include <felitronics/toml/Toml.h>
#include "Fixtures.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace felitronics::toml;
namespace fs = std::filesystem;

namespace
{
fs::path corpus;
std::size_t validCount = 0, invalidCount = 0;

[[noreturn]] void die (const std::string& message)
{
    std::fprintf (stderr, "make_corpus: %s\n", message.c_str());
    std::exit (1);
}
void save (const fs::path& path, std::string_view bytes)
{
    std::ofstream f (path, std::ios::binary | std::ios::trunc);
    f.write (bytes.data(), static_cast<std::streamsize> (bytes.size()));
    if (! f) die ("cannot write " + path.string());
}
std::string integerText (std::int64_t n)
{
    const auto magnitude = n < 0 ? std::uint64_t (-(n + 1)) + 1 : std::uint64_t (n);
    return (n < 0 ? "-" : "") + fixtures::decimalInteger (magnitude);
}
// mantissa / 10^scale spelled with exactly `scale` fractional digits, and the sign of a negative zero.
std::string decimalText (const Decimal& d)
{
    const auto magnitude = d.mantissa < 0 ? std::uint64_t (-(d.mantissa + 1)) + 1 : std::uint64_t (d.mantissa);
    auto digits = fixtures::decimalInteger (magnitude);
    if (digits.size() <= d.scale) digits.insert (0, d.scale + 1 - digits.size(), '0');
    const auto point = digits.size() - d.scale;
    return (d.mantissa < 0 || d.negativeZero ? "-" : "") + digits.substr (0, point) + "." + digits.substr (point);
}
std::string scalar (const char* type, const std::string& value)
{
    return std::string ("{\"type\": \"") + type + "\", \"value\": " + fixtures::jsonString (value) + "}";
}
std::string tree (const Table& t, const std::string& indent);
std::string tagged (const Value& v, const std::string& indent)
{
    if (const auto* s = std::get_if<std::string> (&v.data)) return scalar ("string", *s);
    if (const auto* i = std::get_if<std::int64_t> (&v.data)) return scalar ("integer", integerText (*i));
    if (const auto* d = std::get_if<Decimal> (&v.data)) return scalar ("decimal", decimalText (*d));
    if (const auto* b = std::get_if<bool> (&v.data)) return scalar ("bool", *b ? "true" : "false");
    if (const auto* t = std::get_if<Table> (&v.data)) return tree (*t, indent);
    std::string out = "[";
    if (const auto* a = std::get_if<Array> (&v.data))
        for (std::size_t i = 0; i < a->size(); ++i) out += (i ? ", " : "") + tagged ((*a)[i], indent);
    if (const auto* a = std::get_if<Tables> (&v.data))
        for (std::size_t i = 0; i < a->size(); ++i)
            out += (i ? ",\n" : "\n") + indent + "  " + tree ((*a)[i], indent + "  ") + (i + 1 == a->size() ? "\n" + indent : "");
    return out + "]";
}
std::string tree (const Table& t, const std::string& indent)
{
    if (t.entries().empty()) return "{}";
    std::string out = "{";
    for (std::size_t i = 0; i < t.entries().size(); ++i)
    {
        const auto& e = t.entries()[i];
        out += (i ? ",\n" : "\n") + indent + "  " + fixtures::jsonString (e.key) + ": " + tagged (e.value, indent + "  ");
    }
    return out + "\n" + indent + "}";
}

void valid (const std::string& name, std::string_view text)
{
    const auto result = parse (text);
    const auto* root = std::get_if<Table> (&result);
    if (root == nullptr) die ("valid/" + name + " is refused: " + codeName (std::get<Error> (result).code));
    const auto dir = corpus / "valid";
    save (dir / (name + ".toml"), text);
    save (dir / (name + ".json"), tree (*root, "") + "\n");
    const auto canonical = write (*root);
    if (canonical != text) save (dir / (name + ".canonical.toml"), canonical);
    ++validCount;
}
void invalid (const std::string& name, std::string_view text, Code code, std::uint32_t line, std::uint32_t column)
{
    const auto result = parse (text);
    const auto* e = std::get_if<Error> (&result);
    if (e == nullptr || ! (*e == Error { code, line, column }))
        die ("invalid/" + name + ": the parser disagrees with the hand-written expectation");
    const auto dir = corpus / "invalid";
    save (dir / (name + ".toml"), text);
    save (dir / (name + ".json"), "{\"code\": \"" + std::string (codeName (code)) + "\", \"line\": "
                                    + fixtures::decimalInteger (line) + ", \"column\": " + fixtures::decimalInteger (column) + "}\n");
    ++invalidCount;
}
std::string hex (unsigned char c)
{
    return { "0123456789abcdef"[c >> 4], "0123456789abcdef"[c & 15] };
}
std::string hexBytes (const std::string& s)
{
    std::string out;
    for (const char c : s) out += hex (static_cast<unsigned char> (c));
    return out;
}
std::string path (std::size_t n)
{
    std::string out = "a";
    for (std::size_t i = 1; i < n; ++i) out += ".a";
    return out;
}
std::string number (std::size_t n) { return fixtures::decimalInteger (n); }
}

int main (int argc, char** argv)
{
    if (argc != 2) die ("usage: felitronics_toml_make_corpus <corpus directory>");
    corpus = argv[1];
    fs::create_directories (corpus / "valid");
    fs::create_directories (corpus / "invalid");

    // --- the minimal witness of every error code, and positions in UTF-8 bytes -------------------------------
    invalid ("bom", "\xEF\xBB\xBF", Code::Bom, 1, 1);
    invalid ("invalid-utf8-continuation-byte", "\x80", Code::InvalidUtf8, 1, 1);
    invalid ("raw-nul", std::string (1, '\0'), Code::InvalidControl, 1, 1);
    invalid ("bare-cr", "\r", Code::BareCarriageReturn, 1, 1);
    invalid ("expected-key", "=", Code::ExpectedKey, 1, 1);
    invalid ("expected-equals", "a", Code::ExpectedEquals, 1, 2);
    invalid ("expected-value", "a=", Code::ExpectedValue, 1, 3);
    invalid ("literal-string", "a='x'", Code::UnsupportedValue, 1, 3);
    invalid ("unterminated-string", "a=\"", Code::UnterminatedString, 1, 4);
    invalid ("invalid-escape", "a=\"\\z\"", Code::InvalidEscape, 1, 5);
    invalid ("unicode-escape-nonhex", "a=\"\\uX\"", Code::InvalidUnicodeEscape, 1, 6);
    invalid ("integer-leading-zeros", "a=00", Code::InvalidNumber, 1, 3);
    invalid ("integer-above-int64", "a=9223372036854775808", Code::IntegerRange, 1, 3);
    invalid ("decimal-ten-fraction-digits", "a=0.0000000000", Code::DecimalScale, 1, 14);
    invalid ("decimal-mantissa-above-2p53", "a=900719925474099.3", Code::DecimalRange, 1, 3);
    invalid ("array-missing-comma", "a=[0 0]", Code::ExpectedArraySeparator, 1, 6);
    invalid ("unterminated-array", "a=[", Code::UnterminatedArray, 1, 4);
    invalid ("mixed-array", "a=[0,true]", Code::MixedArray, 1, 6);
    invalid ("header-unclosed", "[a", Code::ExpectedHeaderEnd, 1, 3);
    invalid ("trailing-characters", "a=0 x", Code::TrailingCharacters, 1, 5);
    invalid ("duplicate-key", "a=0\na=1", Code::DuplicateKey, 2, 1);
    invalid ("redefined-table", "[a]\n[a]", Code::RedefinedTable, 2, 2);
    invalid ("value-then-table", "a=0\n[a]", Code::TableValueConflict, 2, 2);
    invalid ("invalid-escape-after-crlf-and-utf8", "# \xC3\xA9\r\n\t a = \"x\\q\"", Code::InvalidEscape, 2, 10);
    invalid ("syntax-error-before-invalid-utf8", "=\n\x80", Code::ExpectedKey, 1, 1);
    invalid ("invalid-utf8-before-duplicate-key", "a=\"\x80\"\na=1", Code::InvalidUtf8, 1, 4);
    invalid ("trailing-after-utf8-string", "a=\"\xC3\xA9\" x", Code::TrailingCharacters, 1, 8);

    // --- table ownership ---------------------------------------------------------------------------------------
    invalid ("dotted-table-then-header", "a.b=1\n[a]", Code::RedefinedTable, 2, 2);
    invalid ("dotted-key-reopens-header-table", "[a.b]\n[a]\nb.c=1", Code::RedefinedTable, 3, 1);
    invalid ("header-repeated-after-child", "[a]\n[a.b]\n[a]", Code::RedefinedTable, 3, 2);
    invalid ("value-replaces-dotted-table", "a.b=1\na=2", Code::TableValueConflict, 2, 1);
    invalid ("dotted-key-through-value", "a=1\na.b=2", Code::TableValueConflict, 2, 1);
    invalid ("table-then-array-of-tables", "[a]\n[[a]]", Code::TableValueConflict, 2, 3);
    invalid ("array-of-tables-then-table", "[[a]]\n[a]", Code::TableValueConflict, 2, 2);
    invalid ("array-then-array-of-tables", "a=[]\n[[a]]", Code::TableValueConflict, 2, 3);
    invalid ("duplicate-key-in-table", "[a]\nx=1\nx=2", Code::DuplicateKey, 3, 1);
    invalid ("duplicate-key-quoted-spelling", "a=1\n\"a\"=2", Code::DuplicateKey, 2, 1);

    // --- TOML 1.0 values outside the subset, and malformed values ---------------------------------------------
    const struct { const char* name; const char* value; Code code; } values[] = {
        { "leading-zero", "01", Code::InvalidNumber }, { "leading-zero-negative", "-01", Code::InvalidNumber },
        { "leading-zero-plus", "+01", Code::InvalidNumber }, { "leading-zero-decimal", "01.1", Code::InvalidNumber },
        { "decimal-without-fraction", "1.", Code::InvalidNumber }, { "decimal-without-integer", ".1", Code::UnsupportedValue },
        { "exponent", "1e2", Code::InvalidNumber }, { "decimal-exponent", "1.0e2", Code::InvalidNumber },
        { "underscore", "1_000", Code::InvalidNumber }, { "hexadecimal", "0xff", Code::InvalidNumber },
        { "octal", "0o77", Code::InvalidNumber }, { "binary", "0b01", Code::InvalidNumber },
        { "sign-only-plus", "+", Code::InvalidNumber }, { "sign-only-minus", "-", Code::InvalidNumber },
        { "bool-capitalized", "True", Code::UnsupportedValue }, { "bool-trailing-letter", "falsee", Code::InvalidNumber },
        { "inf", "inf", Code::UnsupportedValue }, { "nan", "nan", Code::UnsupportedValue },
        { "local-date", "1979-05-27", Code::InvalidNumber }, { "local-time", "12:00:00", Code::InvalidNumber } };
    for (const auto& v : values) invalid (std::string ("value-") + v.name, std::string ("a=") + v.value, v.code, 1, 3);
    invalid ("nested-array", "a=[[0]]", Code::UnsupportedValue, 1, 4);
    invalid ("inline-table", "a={}", Code::UnsupportedValue, 1, 3);
    invalid ("multiline-string", "a=\"\"\"x\"\"\"", Code::TrailingCharacters, 1, 5);
    invalid ("string-raw-newline", "a=\"x\ny\"", Code::UnterminatedString, 1, 5);
    invalid ("unicode-escape-surrogate", "a=\"\\uD800\"", Code::InvalidUnicodeEscape, 1, 4);
    invalid ("unicode-escape-above-10ffff", "a=\"\\U00110000\"", Code::InvalidUnicodeEscape, 1, 4);
    invalid ("unicode-escape-ffffffff", "a=\"\\UFFFFFFFF\"", Code::InvalidUnicodeEscape, 1, 4);
    invalid ("string-ends-in-backslash", "a=\"\\", Code::UnterminatedString, 1, 5);
    invalid ("unicode-escape-truncated", "a=\"\\u12", Code::InvalidUnicodeEscape, 1, 8);
    invalid ("unterminated-array-after-comma", "a=[1,", Code::UnterminatedArray, 1, 6);
    invalid ("array-leading-comma", "a=[,]", Code::ExpectedValue, 1, 4);
    invalid ("array-double-comma", "a=[1,,]", Code::ExpectedValue, 1, 6);
    invalid ("array-of-tables-header-unclosed", "[[a]", Code::ExpectedHeaderEnd, 1, 5);

    // --- every resource at the limit (valid) and at limit + 1 (invalid) ---------------------------------------
    valid ("limit-document", std::string (kMaxDocument, ' '));
    invalid ("limit-document", std::string (kMaxDocument + 1, ' '), Code::DocumentLimit, 1, std::uint32_t (kMaxDocument + 1));
    valid ("limit-depth-dotted", path (kMaxDepth) + "=0");
    invalid ("limit-depth-dotted", path (kMaxDepth + 1) + "=0", Code::DepthLimit, 1, 33);
    valid ("limit-depth-header", "[" + path (kMaxDepth) + "]");
    invalid ("limit-depth-header", "[" + path (kMaxDepth + 1) + "]", Code::DepthLimit, 1, 34);
    invalid ("limit-depth-header-plus-key", "[" + path (kMaxDepth) + "]\nx=0", Code::DepthLimit, 2, 1);
    valid ("limit-bare-key", std::string (kMaxKey, 'a') + "=0");
    invalid ("limit-bare-key", std::string (kMaxKey + 1, 'a') + "=0", Code::KeyLimit, 1, 257);
    valid ("limit-quoted-key", "\"" + std::string (kMaxKey, 'a') + "\"=0");
    invalid ("limit-quoted-key", "\"" + std::string (kMaxKey + 1, 'a') + "\"=0", Code::KeyLimit, 1, 258);
    valid ("limit-string", "a=\"" + std::string (kMaxString, 'x') + "\"");
    invalid ("limit-string", "a=\"" + std::string (kMaxString + 1, 'x') + "\"", Code::StringLimit, 1, 65540);
    std::string array = "a=[";
    for (std::size_t i = 0; i < kMaxArray; ++i) array += "0,";
    valid ("limit-array", array + "]");
    invalid ("limit-array", array + "0]", Code::ArrayLimit, 1, 131076);
    std::string entries;
    for (std::size_t i = 0; i < kMaxEntries; ++i) entries += "a" + number (i) + "=0\n";
    valid ("limit-entries", entries);
    invalid ("limit-entries", entries + "x=0", Code::EntryLimit, 65537, 1);
    std::string tables; // the container is one entry, every element another
    for (std::size_t i = 0; i < kMaxEntries - 1; ++i) tables += "[[a]]\n";
    valid ("limit-entries-array-of-tables", tables);
    invalid ("limit-entries-array-of-tables", tables + "[[a]]", Code::EntryLimit, 65536, 3);
    Table full; // a canonical document of exactly 1 MiB, built from strings as tests/GrammarTests.cpp builds it
    for (int i = 0; i < 15; ++i) fixtures::put (full, "s" + number (std::size_t (i)), std::string (65536, 'x'));
    fixtures::put (full, "tail", std::string (kMaxDocument - write (full).size() - 10, 'x'));
    const auto exact = write (full);
    if (exact.size() != kMaxDocument) die ("the exact canonical document is not 1 MiB");
    valid ("limit-canonical", exact);
    auto compact = exact; // optional spaces removed, one byte added: the input fits, its canonical form does not
    const auto space = compact.find (" = ");
    compact.erase (space + 2, 1);
    compact.erase (space, 1);
    compact.insert (compact.size() - 2, "x");
    invalid ("limit-canonical", compact, Code::CanonicalLimit, 17, 1);

    // --- hostile input: must end in an error, not in a crash, a stack overflow or quadratic time --------------
    invalid ("hostile-open-brackets", std::string (kMaxDocument, '['), Code::ExpectedKey, 1, 3);
    invalid ("hostile-deep-path", path (100000), Code::DepthLimit, 1, 33);
    std::string longArray = "a=[";
    for (int i = 0; i < 100000; ++i) longArray += "0,";
    invalid ("hostile-long-array", longArray + "]", Code::ArrayLimit, 1, 131076);
    invalid ("hostile-long-string", "a=\"" + std::string (900000, 'x'), Code::StringLimit, 1, 65540);

    // --- every invalid UTF-8 class in every context, every forbidden control byte ------------------------------
    for (const auto& bad : { std::string ("\x80"), std::string ("\xC0\xAF"), std::string ("\xC1\xBF"),
            std::string ("\xC2"), std::string ("\xE0\x80\x80"), std::string ("\xED\xA0\x80"),
            std::string ("\xF0\x80\x80\x80"), std::string ("\xF4\x90\x80\x80"), std::string ("\xF5\x80\x80\x80"),
            std::string ("\xFE"), std::string ("\xFF"), std::string ("\xE2\x82"), std::string ("\xE2x\xA0") })
    {
        const auto tag = "utf8-" + hexBytes (bad);
        invalid (tag + "-in-comment", "#" + bad, Code::InvalidUtf8, 1, 2);
        invalid (tag + "-in-key", "\"" + bad + "\"=0", Code::InvalidUtf8, 1, 2);
        invalid (tag + "-in-string", "a=\"" + bad + "\"", Code::InvalidUtf8, 1, 4);
        invalid (tag + "-in-header", "[\"" + bad + "\"]", Code::InvalidUtf8, 1, 3);
        invalid (tag + "-in-array", "a=[\"" + bad + "\"]", Code::InvalidUtf8, 1, 5);
    }
    for (int b = 0; b < 128; ++b)
        if ((b < 32 && b != 9 && b != 10 && b != 13) || b == 127)
        {
            const std::string raw (1, char (b));
            const auto tag = "control-" + hex (static_cast<unsigned char> (b));
            invalid (tag + "-in-comment", "#" + raw, Code::InvalidControl, 1, 2);
            invalid (tag + "-in-string", "a=\"" + raw + "\"", Code::InvalidControl, 1, 4);
            invalid (tag + "-in-key", "\"" + raw + "\"=1", Code::InvalidControl, 1, 2);
        }
    invalid ("bare-cr-in-comment", "# hi\r", Code::BareCarriageReturn, 1, 5);
    invalid ("bare-cr-in-string", "a=\"x\r\"", Code::BareCarriageReturn, 1, 5);
    // One invalid byte at every offset of a document that uses every construct: the encoding error wins.
    const std::string everywhere = "# hi\n\"name\"=\"text\"\n[a.b]\nx=[true,false]\n[[c]]\nx=-1.25\n";
    for (std::size_t i = 0; i <= everywhere.size(); ++i)
    {
        std::uint32_t line = 1, column = 1;
        for (std::size_t j = 0; j < i; ++j)
            if (everywhere[j] == '\n') { ++line; column = 1; } else ++column;
        for (const char byte : { '\x80', '\0' })
        {
            auto changed = everywhere;
            changed.insert (i, 1, byte);
            const auto n = number (i);
            invalid ("inserted-" + hex (static_cast<unsigned char> (byte)) + "-at-" + std::string (3 - n.size(), '0') + n,
                     changed, byte == 0 ? Code::InvalidControl : Code::InvalidUtf8, line, column);
        }
    }

    // --- accepted grammar -----------------------------------------------------------------------------------
    valid ("empty", "");
    valid ("whitespace-and-comment", " \t# hi");
    valid ("crlf-and-plus-zero", "\r\n#c\r\na=+0\r\n");
    valid ("empty-quoted-key", "\"\"=true");
    valid ("numeric-bare-key", "123=42");
    valid ("empty-string", "a=\"\"");
    valid ("array-comments-and-trailing-comma", "a=[ # hi\n1, # next\r\n2,\n] #done");
    valid ("empty-array", "a=[]");
    valid ("raw-tab-in-string", "a=\"raw\ttab\"");
    valid ("unicode-escapes", "a=\"\\u0000\\u007F\\U0010FFFF\"");
    valid ("dotted-quoted-key", "a . \"b.c\" = false");
    valid ("header-quoted-dotted", "[ a . \"b.c\" ]\nx=1");
    valid ("array-of-tables-dotted", "[[ a . b ]]\nx=1\n[[a.b]]\nx=2");
    valid ("implicit-parent-then-header", "[a.b]\nx=1\n[a]\ny=2");
    valid ("dotted-shared-prefix", "a.b.c=1\na.b.d=2");
    valid ("dotted-then-sibling-header", "a.b=1\n[a.c]\nx=2");
    valid ("subtable-per-array-element", "[[a]]\nx=1\n[a.b]\ny=2\n[[a]]\nx=3\n[a.b]\ny=4");
    valid ("nested-arrays-of-tables", "[[a]]\n[[a.b]]\nx=1\n[[a.b]]\nx=2\n[[a]]\n[[a.b]]\nx=3");
    valid ("integer-int64-edges", "a=-9223372036854775808\nb=9223372036854775807");
    valid ("decimal-mantissa-edges", "a=900719925474099.2\nb=-9007199.254740992");
    valid ("decimal-signed-zero-and-scale", "a=[-0.0,0.00,-1.000]");
    valid ("bool-array", "a=[true,false]");
    valid ("utf8-string-array", "a=[\"\xC3\xA9\",\"\xF0\x9F\x98\x80\"]");
    valid ("empty-tables", "[a]\n[b]\n");
    valid ("utf8-encoding-boundaries", "a=\"\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xF0\x90\x80\x80\xF4\x8F\xBF\xBF\"");
    valid ("writer-groups-scalars-tables-arrays", "sub.z = false\nb = 1\na = -0.00\n\"\" = \"\xC3\xA9\\n\\\\\\\"\"\n[[array]]\n");

    // --- generated documents: the first 16 of the 512 the property suite generates, then its depth-16 tree ----
    fixtures::Generator g { 0x95E914F275AE5811ULL };
    for (int i = 0; i < 16; ++i)
    {
        const auto n = number (std::size_t (i));
        valid ("generated-" + std::string (3 - n.size(), '0') + n, write (fixtures::generated (g)));
    }
    Table depth;
    for (int i = 0; i < 16; ++i) { Table parent; fixtures::put (parent, "a", std::move (depth)); depth = std::move (parent); }
    valid ("generated-depth-16", write (depth));

    std::printf ("corpus: %zu valid and %zu invalid documents written to %s\n", validCount, invalidCount, corpus.string().c_str());
    return 0;
}

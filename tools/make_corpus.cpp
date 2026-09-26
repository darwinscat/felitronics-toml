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
#include "SchemaCases.h"
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
std::size_t validCount = 0, invalidCount = 0, overlayCount = 0, schemaCount = 0;

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
    else fs::remove (dir / (name + ".canonical.toml")); // a reseed must not leave a stale companion
    // Where every value and key was written. The megabyte limit documents would add megabytes of positions
    // and nothing new, so documents from 64 KiB up have none.
    if (text.size() < 65536) save (dir / (name + ".positions.json"), fixtures::positionsJson (*root));
    else fs::remove (dir / (name + ".positions.json"));
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
// overlay/<name>: the base parsed with source 1, the top with source 2, and what overlay() makes of them.
void layered (const std::string& name, std::string_view base, std::string_view top)
{
    auto b = parse (base, 1), t = parse (top, 2);
    if (! std::holds_alternative<Table> (b) || ! std::holds_alternative<Table> (t)) die ("overlay/" + name + ": a layer is refused");
    const auto merged = overlay (std::get<Table> (b), std::get<Table> (t));
    const auto dir = corpus / "overlay";
    save (dir / (name + ".base.toml"), base);
    save (dir / (name + ".top.toml"), top);
    save (dir / (name + ".json"), tree (merged, "") + "\n");
    save (dir / (name + ".canonical.toml"), write (merged));
    save (dir / (name + ".positions.json"), fixtures::positionsJson (merged, true));
    ++overlayCount;
}
// schema/<name>: a document, the fields read from it, the values read and the problems, each problem written here by hand.
using schema_cases::Field;
using schema_cases::Expected;
Field scalarField (std::string key, std::string type, bool optional = false, std::string min = {}, std::string max = {})
{
    Field f;
    f.key = std::move (key); f.type = std::move (type); f.optional = optional; f.min = std::move (min); f.max = std::move (max);
    return f;
}
Field arrayField (std::string key, std::string of, bool optional = false, std::string min = {}, std::string max = {})
{
    auto f = scalarField (std::move (key), "array", optional, std::move (min), std::move (max));
    f.of = std::move (of);
    return f;
}
Field tableField (std::string key, std::string type, std::vector<Field> fields, bool optional = false)
{
    auto f = scalarField (std::move (key), std::move (type), optional);
    f.fields = std::move (fields);
    return f;
}
std::string fieldsJson (const std::vector<Field>& fields, const std::string& indent)
{
    std::string out = "[";
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        const auto& f = fields[i];
        out += (i ? ",\n" : "\n") + indent + "  {\"key\": " + fixtures::jsonString (f.key) + ", \"type\": \"" + f.type + "\"";
        if (! f.of.empty()) out += ", \"of\": \"" + f.of + "\"";
        if (f.optional) out += ", \"optional\": true";
        if (! f.min.empty()) out += ", \"min\": " + fixtures::jsonString (f.min);
        if (! f.max.empty()) out += ", \"max\": " + fixtures::jsonString (f.max);
        if (f.type == "table" || f.type == "tables") out += ", \"fields\": " + fieldsJson (f.fields, indent + "  ");
        out += "}";
    }
    return out + (fields.empty() ? "]" : "\n" + indent + "]");
}
void schemaCase (const std::string& name, std::string_view text, const schema_cases::Schema& schema, const std::vector<Expected>& expected)
{
    const auto result = parse (text);
    if (! std::holds_alternative<Table> (result)) die ("schema/" + name + ": the document is refused");
    Report report;
    const auto values = schema_cases::run (std::get<Table> (result), schema, report);
    if (schema_cases::spelled (schema_cases::found (report)) != schema_cases::spelled (expected))
        die ("schema/" + name + ": the reader disagrees with the hand-written problems:\n" + schema_cases::spelled (schema_cases::found (report)));
    std::string problems = "[";
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        const auto& e = expected[i];
        problems += std::string (i ? ",\n" : "\n") + "    {\"fault\": \"" + faultName (e.fault) + "\", \"severity\": \""
                  + (e.severity == Severity::Error ? "error" : "warning") + "\", \"path\": " + fixtures::jsonString (e.path)
                  + ", \"line\": " + fixtures::decimalInteger (e.line) + ", \"column\": " + fixtures::decimalInteger (e.column) + "}";
    }
    problems += expected.empty() ? "]" : "\n  ]";
    const auto dir = corpus / "schema";
    save (dir / (name + ".toml"), text);
    save (dir / (name + ".json"), std::string ("{\n  \"unknownKeys\": \"") + (schema.unknownKeys == Severity::Error ? "error" : "warning")
                                   + "\",\n  \"fields\": " + fieldsJson (schema.fields, "  ") + ",\n  \"read\": " + tree (values, "  ")
                                   + ",\n  \"problems\": " + problems + "\n}\n");
    ++schemaCount;
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
    fs::create_directories (corpus / "overlay");
    fs::create_directories (corpus / "schema");

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
    invalid ("trailing-after-utf8-string", "a=\"\xC3\xA9\" x", Code::TrailingCharacters, 1, 7);
    invalid ("bare-value-runs-into-invalid-utf8", "a=0.0000000000\x80", Code::InvalidUtf8, 1, 15);
    for (const auto& c : fixtures::codePointColumns()) invalid (c.name, c.text, c.code, c.line, c.column);

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
        { "hexadecimal", "0xff", Code::InvalidNumber },
        { "octal", "0o77", Code::InvalidNumber }, { "binary", "0b01", Code::InvalidNumber },
        { "sign-only-plus", "+", Code::InvalidNumber }, { "sign-only-minus", "-", Code::InvalidNumber },
        { "bool-capitalized", "True", Code::UnsupportedValue }, { "bool-trailing-letter", "falsee", Code::InvalidNumber },
        { "inf", "inf", Code::UnsupportedValue }, { "nan", "nan", Code::UnsupportedValue },
        { "local-date", "1979-05-27", Code::InvalidNumber }, { "local-time", "12:00:00", Code::InvalidNumber } };
    for (const auto& v : values) invalid (std::string ("value-") + v.name, std::string ("a=") + v.value, v.code, 1, 3);
    for (const auto& [name, value] : { std::pair { "double", "1__000" }, std::pair { "trailing", "1000_" },
            std::pair { "after-plus", "+_1" }, std::pair { "after-minus", "-_1" }, std::pair { "before-point", "1_.5" },
            std::pair { "after-point", "1._5" }, std::pair { "trailing-fraction", "1.5_" }, std::pair { "leading-zero", "0_1" },
            std::pair { "before-exponent", "1_e2" } })
        invalid (std::string ("underscore-") + name, std::string ("a=") + value, Code::InvalidNumber, 1, 3);
    invalid ("underscore-leading", "a=_1", Code::UnsupportedValue, 1, 3);
    invalid ("underscore-tenth-fraction-digit", "a=0.000_000_000_1", Code::DecimalScale, 1, 17);
    invalid ("underscore-above-int64", "a=9_223_372_036_854_775_808", Code::IntegerRange, 1, 3);
    invalid ("underscore-in-array-item", "a=[1_0, 2__0]", Code::InvalidNumber, 1, 9);
    invalid ("nested-array", "a=[[0]]", Code::UnsupportedValue, 1, 4);
    // --- inline tables: one line, no trailing comma, a value closed to later headers and dotted keys --------------
    invalid ("inline-trailing-comma", "a={x=1,}", Code::ExpectedKey, 1, 8);
    invalid ("inline-leading-comma", "a={,}", Code::ExpectedKey, 1, 4);
    invalid ("inline-line-ending-before-brace", "a={x=1\n}", Code::UnterminatedInlineTable, 1, 7);
    invalid ("inline-line-ending-after-comma", "a={x=1,\ny=2}", Code::UnterminatedInlineTable, 1, 8);
    invalid ("inline-crlf-before-brace", "a={x=1\r\n}", Code::UnterminatedInlineTable, 1, 7);
    invalid ("inline-unterminated", "a={", Code::UnterminatedInlineTable, 1, 4);
    invalid ("inline-comment", "a={ # c\n}", Code::UnterminatedInlineTable, 1, 5);
    invalid ("inline-line-ending-after-key", "a={x\n}", Code::UnterminatedInlineTable, 1, 5);
    invalid ("inline-line-ending-after-equals", "a={x=\n1}", Code::UnterminatedInlineTable, 1, 6);
    invalid ("inline-eof-after-key", "a={x", Code::UnterminatedInlineTable, 1, 5);
    invalid ("inline-eof-after-equals", "a={x=", Code::UnterminatedInlineTable, 1, 6);
    invalid ("inline-comment-after-equals", "a={x = # c\n1}", Code::UnterminatedInlineTable, 1, 8);
    invalid ("inline-line-ending-in-dotted-key", "a={b.\nc=1}", Code::UnterminatedInlineTable, 1, 6);
    invalid ("inline-missing-separator", "a={x=1 y=2}", Code::ExpectedInlineTableSeparator, 1, 8);
    invalid ("inline-closed-by-bracket", "a={x=1]", Code::ExpectedInlineTableSeparator, 1, 7);
    invalid ("inline-missing-value", "a={x=}", Code::ExpectedValue, 1, 6);
    invalid ("inline-missing-equals", "a={x}", Code::ExpectedEquals, 1, 5);
    invalid ("inline-extra-brace", "a={x=1}}", Code::TrailingCharacters, 1, 8);
    invalid ("inline-duplicate-key", "a={x=1,x=2}", Code::DuplicateKey, 1, 8);
    invalid ("inline-dotted-table-then-value", "a={x.y=1,x=2}", Code::TableValueConflict, 1, 10);
    invalid ("inline-nested-then-dotted-key", "a={x={z=1},x.y=2}", Code::TableValueConflict, 1, 12);
    invalid ("inline-then-dotted-key", "a={}\na.b=1", Code::TableValueConflict, 2, 1);
    invalid ("inline-then-header", "a={}\n[a]", Code::TableValueConflict, 2, 2);
    invalid ("inline-then-subtable-header", "a={x=1}\n[a.y]", Code::TableValueConflict, 2, 2);
    invalid ("inline-then-array-of-tables-header", "a={x=1}\n[[a.y]]", Code::TableValueConflict, 2, 3);
    invalid ("inline-array-then-array-of-tables", "a=[{}]\n[[a]]", Code::TableValueConflict, 2, 3);
    invalid ("inline-array-then-subtable-header", "a=[{}]\n[a.b]", Code::TableValueConflict, 2, 2);
    invalid ("inline-then-value", "a={}\na=1", Code::DuplicateKey, 2, 1);
    invalid ("dotted-table-then-inline", "a.b=1\na={}", Code::TableValueConflict, 2, 1);
    invalid ("header-table-then-inline", "[t.a]\n[t]\na={}", Code::TableValueConflict, 3, 1);
    invalid ("inline-array-scalar-then-table", "a=[1,{}]", Code::MixedArray, 1, 6);
    invalid ("inline-array-table-then-scalar", "a=[{},1]", Code::MixedArray, 1, 7);
    invalid ("inline-array-nested-array", "a=[{},[1]]", Code::UnsupportedValue, 1, 7);
    invalid ("inline-array-element-error-first", "a=[{x=01},1]", Code::InvalidNumber, 1, 7);
    invalid ("inline-array-missing-comma", "a=[{} {}]", Code::ExpectedArraySeparator, 1, 7);
    invalid ("inline-array-unterminated", "a=[{}", Code::UnterminatedArray, 1, 6);
    invalid ("multiline-unterminated", "a=\"\"\"x", Code::UnterminatedString, 1, 7);
    invalid ("multiline-unterminated-after-line", "a=\"\"\"x\n", Code::UnterminatedString, 2, 1);
    invalid ("multiline-six-closing-quotes", "a=\"\"\"x\"\"\"\"\"\"", Code::TrailingCharacters, 1, 12);
    invalid ("multiline-backslash-before-text", "a=\"\"\"a\\ b\"\"\"", Code::InvalidEscape, 1, 8);
    invalid ("multiline-backslash-before-eof", "a=\"\"\"a\\  ", Code::InvalidEscape, 1, 8);
    invalid ("multiline-unknown-escape", "a=\"\"\"\n\\q\"\"\"", Code::InvalidEscape, 2, 2);
    invalid ("multiline-bare-cr", "a=\"\"\"x\ry\"\"\"", Code::BareCarriageReturn, 1, 7);
    invalid ("multiline-control", "a=\"\"\"\n\x01\"\"\"", Code::InvalidControl, 2, 1);
    invalid ("multiline-key", "\"\"\"a\"\"\" = 1", Code::ExpectedEquals, 1, 3);
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
    // An inline table's keys continue its key's path; an element's keys are one level below its array's key.
    valid ("limit-depth-inline", path (kMaxDepth - 1) + " = { x = 1 }");
    invalid ("limit-depth-inline", path (kMaxDepth) + " = { x = 1 }", Code::DepthLimit, 1, 37);
    valid ("limit-depth-inline-array", "[" + path (kMaxDepth - 2) + "]\nb = [{ c = 1 }]");
    invalid ("limit-depth-inline-array", "[" + path (kMaxDepth - 1) + "]\nb = [{ c = 1 }]", Code::DepthLimit, 2, 8);
    std::string nested = "a = ", close;
    for (std::size_t i = 1; i < kMaxDepth; ++i) { nested += "{ a = "; close += " }"; }
    valid ("limit-depth-nested-inline", nested + "1" + close);
    invalid ("limit-depth-nested-inline", nested + "{ a = 1 }" + close, Code::DepthLimit, 1, 97);
    std::string rowsOpen, rowsClose; // arrays of inline tables inside each other: each level one key deeper
    for (std::size_t i = 1; i < kMaxDepth; ++i) { rowsOpen += "a = [{ "; rowsClose += " }]"; }
    valid ("limit-depth-nested-inline-arrays", rowsOpen + "x = 1" + rowsClose);
    invalid ("limit-depth-nested-inline-arrays", rowsOpen + "a = [{ x = 1 }]" + rowsClose, Code::DepthLimit, 1, 113);
    valid ("limit-bare-key", std::string (kMaxKey, 'a') + "=0");
    invalid ("limit-bare-key", std::string (kMaxKey + 1, 'a') + "=0", Code::KeyLimit, 1, 257);
    valid ("limit-quoted-key", "\"" + std::string (kMaxKey, 'a') + "\"=0");
    invalid ("limit-quoted-key", "\"" + std::string (kMaxKey + 1, 'a') + "\"=0", Code::KeyLimit, 1, 258);
    valid ("limit-string", "a=\"" + std::string (kMaxString, 'x') + "\"");
    invalid ("limit-string", "a=\"" + std::string (kMaxString + 1, 'x') + "\"", Code::StringLimit, 1, 65540);
    // The one or two quotes right before the closing three are content, and count.
    valid ("limit-multiline-string", "a=\"\"\"" + std::string (kMaxString - 2, 'x') + "\"\"\"\"\"");
    invalid ("limit-multiline-string", "a=\"\"\"" + std::string (kMaxString - 1, 'x') + "\"\"\"\"\"", Code::StringLimit, 1, 65542);
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
    // Each element of an inline array counts as it is appended, the array's key after them.
    std::string rows = "a=[";
    for (std::size_t i = 0; i + 1 < kMaxEntries; ++i) rows += "{},";
    valid ("limit-entries-inline-array", rows + "]");
    invalid ("limit-entries-inline-array", rows + "{}]", Code::EntryLimit, 1, 1);
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
    valid ("inline-tables", "a = {}\n"
                            "b = { x = 1, y = \"s\", z = [1, 2], t = { u = true } }\n"
                            "c = { \"quoted key\" = 1, d.e = 2, d.f = -0.50 }\n"
                            "g = {s=\"\"\"line\nline\"\"\",h=[\n1, # a comment\n2,\n]}  # a multi-line value inside is fine\n");
    valid ("inline-array-of-tables", "rows = [\n  { f = 100, gain = -1.5 },\n\t{ f = 1_000, gain = 0.0 }, # a comment\n]\n"
                                     "compact = [{a=1},{a=2}]\n"
                                     "nested = [{ b = [{ c = 1 }] }]\n");
    valid ("inline-tables-under-headers", "[[a]]\nb = { c = 1 }\n[[a]]\nb = [{ c = 2 }]\n[t]\ni = { j = 6 }\nk = 1\n");
    valid ("inline-table-as-dotted-value", "a.b = { c = 1 }\na.d = 2\n");
    valid ("inline-tables-in-scripts", "\"" + fixtures::kyiv + "\" = { \"" + fixtures::klyuch + "\" = \"" + fixtures::nihongo + "\", \""
                                       + fixtures::smile + "\" = [{ \"" + fixtures::arabiyya + "\" = 1 }] }\n");
    valid ("multiline-strings", "a = \"\"\"\nline one\nline two\"\"\"\n"
                                "b = \"\"\"one line\"\"\"\n"
                                "c = \"\"\"one \\\n    two\"\"\"\n"
                                "d = \"\"\"a\"b\"\"c\"\"\"\n"
                                "e = \"\"\"x\"\"\"\"\n"
                                "f = \"\"\"x\"\"\"\"\"\n"
                                "g = \"\"\"\"\"\"\n"
                                "h = \"\"\"\"\"\"\"\n"
                                "i = \"\"\"\\t\\u00E9\t# not a comment\\\\\"\"\"  # a comment\n"
                                "j = [\"\"\"x\ny\"\"\", \"z\"]\n"
                                "k = \"\"\"\n\"\"\"\n");
    valid ("multiline-crlf-and-line-ending-backslash", "a = \"\"\"\r\nx\r\ny\r\n\"\"\"\r\nb = \"\"\"a\\ \t\r\n\r\n \t b\"\"\"\r\n");
    valid ("multiline-written-back", "a = \"say \\\"hi\\\"\\n\\\"\\\"x\\\"\\t\\\\\\r\\n\"\nb = [\"a\\nb\"]\n\n[t]\nc = \"\\n\"\n");
    valid ("underscores", "a = 48_000\nb = 1_000.000_1\nc = -9_223_372_036_854_775_808\nd = 9_007_199.254_740_992\ne = +1_0\n"
                          "f = [1_0, -2_0]\n");
    valid ("empty-tables", "[a]\n[b]\n");
    valid ("utf8-encoding-boundaries", "a=\"\xC2\x80\xDF\xBF\xE0\xA0\x80\xED\x9F\xBF\xEE\x80\x80\xF0\x90\x80\x80\xF4\x8F\xBF\xBF\"");
    for (const auto& [name, text] : fixtures::unicodeDocuments()) valid (name, text);
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

    // --- overlay: tables merge key by key, everything else is replaced whole ------------------------------------
    layered ("scalars-replace-and-append", "a = 1\nb = \"x\"\nc = true\n", "b = \"y\"\nd = 4\n");
    layered ("tables-merge", "[limiter]\nceiling = -1.0\nrelease = 0.050\n[limiter.detector]\nmode = \"peak\"\n[eq]\nlow = 0.0\n",
             "[limiter]\nceiling = -2.0\nlookahead = 5\ndetector.mode = \"rms\"\n");
    layered ("arrays-replace", "a = [1, 2, 3]\nrows = [{ x = 1 }, { x = 2 }]\n[[bands]]\nf = 100\n[[bands]]\nf = 200\n",
             "a = [9]\nrows = []\n[[bands]]\nf = 1000\n");
    layered ("type-changes-replace", "a = { x = 1 }\nb = 1\nc = [1]\n[d]\ne = 1\n[[f]]\ng = 1\n",
             "a = 5\nb = { y = 2 }\nc = { z = 3 }\nd = \"flat\"\nf = { g = 2 }\n");
    layered ("dotted-header-and-inline-merge", "a.b.c = 1\na.b.d = 2\nt = { x = 1, y = { z = 2 } }\n",
             "[a.b]\nd = 3\ne = 4\n[t.y]\nw = 5\n");
    layered ("order-and-style", "c = 3\nt = { k = 1 }\na = 1\n[h]\nk = 1\n", "a = 10\nnew = true\n[t]\nm = 2\n[h]\nn = 2\n");
    layered ("empty-base", "", "a = 1\n[t]\nb = 2\n");
    layered ("empty-top", "a = 1\n[t]\nb = 2\n", "# nothing to change\n");
    layered ("in-scripts", "\"" + fixtures::kyiv + "\" = { \"" + fixtures::klyuch + "\" = \"" + fixtures::nihongo + "\" }\n\"" + fixtures::smile + "\" = 1\n",
             "[\"" + fixtures::kyiv + "\"]\n\"" + fixtures::arabiyya + "\" = 2\n\"" + fixtures::klyuch + "\" = \"e\u0301\"\n");

    // --- schema: typed reading, unknown keys, integers read as decimals ---------------------------------------
    constexpr auto E = Severity::Error;
    constexpr auto W = Severity::Warning;
    const std::vector<Field> band { scalarField ("f", "integer", false, "20", "20000"), scalarField ("gain", "decimal", false, "-24.0", "24.0"),
                                    scalarField ("type", "string", true) };
    const std::vector<Field> settings {
        scalarField ("name", "string"), scalarField ("ceiling", "decimal", false, "-12.0", "0.0"), scalarField ("rate", "integer", true, "8000", "192000"),
        scalarField ("dither", "bool", true), arrayField ("freqs", "integer", true, "20", "20000"), arrayField ("steps", "decimal", true),
        arrayField ("labels", "string", true), arrayField ("flags", "bool", true),
        tableField ("limiter", "table", { scalarField ("release", "decimal", false, "0.001", "1.0"), scalarField ("lookahead", "integer", true, "0", "50") }),
        tableField ("bands", "tables", band, true), scalarField ("mode", "string", true) };
    schemaCase ("every-type-read", "name = \"Warm master\"\n"
                                   "ceiling = -1.000\n"
                                   "rate = 48_000\n"
                                   "dither = false\n"
                                   "freqs = [20, 1000, 20000]\n"
                                   "steps = [1, 2]        # integers read as 1.0 and 2.0\n"
                                   "labels = [\"a\", \"" + fixtures::kyiv + "\"]\n"
                                   "flags = [true]\n"
                                   "limiter = { release = 0.050 }\n"
                                   "[[bands]]\nf = 100\ngain = 1\n"
                                   "[[bands]]\nf = 1000\ngain = -2.50\ntype = \"shelf\"\n",
                { settings }, {});
    schemaCase ("inline-rows-read-as-headers", "name = \"x\"\nceiling = 0\nlimiter = { release = 1 }\n"
                                               "bands = [\n  { f = 20, gain = -24 },\n  { f = 20000, gain = 24.000000000 },\n]\n",
                { settings }, {});
    schemaCase ("integers-as-decimals", "a = 5\nb = -900719925474099\nc = 900719925474100\nd = 0\ne = -0\nf = [1, -2]\ng = 1.5\n",
                { { scalarField ("a", "decimal"), scalarField ("b", "decimal"), scalarField ("c", "decimal"), scalarField ("d", "decimal"), scalarField ("e", "decimal"),
                    arrayField ("f", "decimal"), scalarField ("g", "integer") } },
                { { Fault::OutOfRange, E, "c", 3, 5 }, { Fault::WrongType, E, "g", 7, 5 } });
    schemaCase ("missing-and-wrong-types", "name = 5\nsteps = 1.0\nflags = [1]\nlabels = \"x\"\n[limiter]\nrelease = \"fast\"\n",
                { settings },
                { { Fault::WrongType, E, "name", 1, 8 }, { Fault::Missing, E, "ceiling", 1, 1 }, { Fault::WrongType, E, "steps", 2, 9 },
                  { Fault::WrongType, E, "labels", 4, 10 }, { Fault::WrongType, E, "flags[0]", 3, 10 },
                  { Fault::WrongType, E, "limiter.release", 6, 11 } });
    schemaCase ("ranges", "name = \"x\"\nceiling = 0.000000001\nrate = 7999\nfreqs = [20, 19, 30000]\n"
                          "limiter = { release = 0.0009, lookahead = 50 }\n[[bands]]\nf = 20001\ngain = -24.000000001\n"
                          "[[bands]]\nf = 20\ngain = -24.00\n",
                { settings },
                { { Fault::OutOfRange, E, "ceiling", 2, 11 }, { Fault::OutOfRange, E, "rate", 3, 8 }, { Fault::OutOfRange, E, "freqs[1]", 4, 14 },
                  { Fault::OutOfRange, E, "limiter.release", 5, 23 }, { Fault::OutOfRange, E, "bands[0].f", 7, 5 },
                  { Fault::OutOfRange, E, "bands[0].gain", 8, 8 } });
    const auto typos = "nmae = \"x\"\nname = \"x\"\nceiling = -1.0\n[limitr]\nrelease = 0.1\n[limiter]\nrelese = 0.1\nrelease = 0.1\n"
                       "[[bands]]\nf = 100\ngain = 0.0\ngian = 1.0\n[\"" + fixtures::klyuch + "\"]\n\"" + fixtures::nihongo + "\" = 1\n";
    schemaCase ("unknown-keys-as-errors", typos, { settings },
                { { Fault::UnknownKey, E, "limiter.relese", 7, 1 }, { Fault::UnknownKey, E, "bands[0].gian", 12, 1 },
                  { Fault::UnknownKey, E, "nmae", 1, 1 }, { Fault::UnknownKey, E, "limitr", 4, 2 },
                  { Fault::UnknownKey, E, "\"" + fixtures::klyuch + "\"", 13, 2 } });
    schemaCase ("unknown-keys-as-warnings", typos, { settings, W },
                { { Fault::UnknownKey, W, "limiter.relese", 7, 1 }, { Fault::UnknownKey, W, "bands[0].gian", 12, 1 },
                  { Fault::UnknownKey, W, "nmae", 1, 1 }, { Fault::UnknownKey, W, "limitr", 4, 2 },
                  { Fault::UnknownKey, W, "\"" + fixtures::klyuch + "\"", 13, 2 } });
    schemaCase ("containers", "name = \"x\"\nceiling = 0.0\nlimiter = 5\nbands = { f = 1 }\nfreqs = 5\n"
                              "rows = [1, 2]\nnone = []\n[\"a b\".\"c.d\"]\nx = 1\n",
                { { scalarField ("name", "string"), scalarField ("ceiling", "decimal"), tableField ("limiter", "table", {}), tableField ("bands", "tables", band),
                    arrayField ("freqs", "integer"), tableField ("rows", "tables", {}), tableField ("none", "tables", band),
                    tableField ("a b", "table", { tableField ("c.d", "table", {}) }) } },
                { { Fault::WrongType, E, "limiter", 3, 11 }, { Fault::WrongType, E, "bands", 4, 9 }, { Fault::WrongType, E, "freqs", 5, 9 },
                  { Fault::WrongType, E, "rows", 6, 8 }, { Fault::UnknownKey, E, "\"a b\".\"c.d\".x", 9, 1 } });
    schemaCase ("missing-in-tables-and-rows", "[limiter]\nlookahead = 1\n[[bands]]\ngain = 1.0\n[[bands]]\nf = 100\n"
                                              "[\"" + fixtures::kyiv + "\".sub]\n",
                { { tableField ("limiter", "table", { scalarField ("release", "decimal") }), tableField ("bands", "tables", band),
                    tableField (fixtures::kyiv, "table", { tableField ("sub", "table", { scalarField ("x", "integer") }), scalarField ("y", "bool") }),
                    tableField ("absent", "table", {}), tableField ("rows", "tables", {}) } },
                { { Fault::Missing, E, "limiter.release", 1, 1 }, { Fault::UnknownKey, E, "limiter.lookahead", 2, 1 },
                  { Fault::Missing, E, "bands[0].f", 3, 1 }, { Fault::Missing, E, "bands[1].gain", 5, 1 },
                  { Fault::Missing, E, "\"" + fixtures::kyiv + "\".sub.x", 7, 1 }, { Fault::Missing, E, "\"" + fixtures::kyiv + "\".y", 7, 2 },
                  { Fault::Missing, E, "absent", 1, 1 }, { Fault::Missing, E, "rows", 1, 1 } });

    std::printf ("corpus: %zu valid, %zu invalid, %zu overlay and %zu schema documents written to %s\n", validCount, invalidCount,
                 overlayCount, schemaCount, corpus.string().c_str());
    return 0;
}

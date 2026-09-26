// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

// The conformance corpus runner. tests/corpus/README.md states the format; this is its C++ reading.
//   valid/<name>.toml    parses to exactly the tree in <name>.json, and write() reproduces <name>.canonical.toml
//                        byte for byte (the .toml itself when there is no .canonical.toml), which parses to the
//                        same tree again.
//   invalid/<name>.toml  is refused with exactly the code, line and column in <name>.json.
#include <felitronics/toml/Toml.h>
#include "toml_test.h"
#include "Fixtures.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

using namespace felitronics::toml;
namespace fs = std::filesystem;

namespace
{
// A small, strict JSON reader: exactly what the corpus needs, nothing shared with the library under test.
struct Json
{
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind kind = Kind::Null;
    std::string text;                 // decoded string, number spelling, or "true" / "false"
    std::vector<std::string> keys;    // object member names, parallel to items
    std::vector<Json> items;          // array elements or object member values
    [[nodiscard]] const Json* member (std::string_view name) const
    {
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == name) return &items[i];
        return nullptr;
    }
};

class JsonReader
{
public:
    explicit JsonReader (std::string_view s) : s_ (s) {}
    [[nodiscard]] std::optional<Json> document()
    {
        Json j = value (0);
        space();
        if (failed_ || p_ != s_.size()) return std::nullopt;
        return j;
    }

private:
    std::string_view s_;
    std::size_t p_ = 0;
    bool failed_ = false;

    void space() { while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t' || s_[p_] == '\n' || s_[p_] == '\r')) ++p_; }
    [[nodiscard]] bool eat (char c)
    {
        space();
        if (p_ < s_.size() && s_[p_] == c) { ++p_; return true; }
        return false;
    }
    Json fail() { failed_ = true; return {}; }
    [[nodiscard]] std::uint32_t hex4()
    {
        std::uint32_t u = 0;
        for (int i = 0; i < 4; ++i, ++p_)
        {
            const char c = p_ < s_.size() ? s_[p_] : '\0';
            const int h = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (h < 0) { failed_ = true; return 0; }
            u = (u << 4) | std::uint32_t (h);
        }
        return u;
    }
    static void utf8 (std::string& out, std::uint32_t u)
    {
        if (u < 0x80) out += char (u);
        else if (u < 0x800) { out += char (0xC0u | (u >> 6)); out += char (0x80u | (u & 63u)); }
        else if (u < 0x10000) { out += char (0xE0u | (u >> 12)); out += char (0x80u | ((u >> 6) & 63u)); out += char (0x80u | (u & 63u)); }
        else
        {
            out += char (0xF0u | (u >> 18)); out += char (0x80u | ((u >> 12) & 63u));
            out += char (0x80u | ((u >> 6) & 63u)); out += char (0x80u | (u & 63u));
        }
    }
    [[nodiscard]] std::string string()
    {
        std::string out;
        ++p_;
        while (p_ < s_.size() && ! failed_)
        {
            const char c = s_[p_++];
            if (c == '"') return out;
            if (static_cast<unsigned char> (c) < 0x20 || p_ == s_.size()) break;
            if (c != '\\') { out += c; continue; }
            switch (s_[p_++])
            {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u':
                {
                    auto u = hex4();
                    if (u >= 0xD800 && u <= 0xDBFF)
                    {
                        if (s_.substr (p_, 2) != "\\u") { failed_ = true; break; }
                        p_ += 2;
                        const auto low = hex4();
                        if (low < 0xDC00 || low > 0xDFFF) { failed_ = true; break; }
                        u = 0x10000 + ((u - 0xD800) << 10) + (low - 0xDC00);
                    }
                    else if (u >= 0xDC00 && u <= 0xDFFF) { failed_ = true; break; }
                    utf8 (out, u);
                    break;
                }
                default: failed_ = true; break;
            }
        }
        failed_ = true;
        return out;
    }
    Json value (int depth)
    {
        space();
        if (depth > 64 || p_ >= s_.size()) return fail();
        Json j;
        const char c = s_[p_];
        if (c == '{' || c == '[')
        {
            const bool object = c == '{';
            j.kind = object ? Json::Kind::Object : Json::Kind::Array;
            ++p_;
            if (eat (object ? '}' : ']')) return j;
            do
            {
                if (object)
                {
                    space();
                    if (p_ >= s_.size() || s_[p_] != '"') return fail();
                    j.keys.push_back (string());
                    if (! eat (':')) return fail();
                }
                j.items.push_back (value (depth + 1));
                if (failed_) return j;
            } while (eat (','));
            if (! eat (object ? '}' : ']')) return fail();
            // A repeated member name would let a table with a key missing still match by count.
            auto names = j.keys;
            std::sort (names.begin(), names.end());
            if (std::adjacent_find (names.begin(), names.end()) != names.end()) return fail();
            return j;
        }
        if (c == '"') { j.kind = Json::Kind::String; j.text = string(); return j; }
        for (const std::string_view word : { "true", "false", "null" })
            if (s_.substr (p_, word.size()) == word)
            {
                p_ += word.size();
                j.kind = word == "null" ? Json::Kind::Null : Json::Kind::Bool;
                j.text = std::string (word);
                return j;
            }
        // -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
        const auto start = p_;
        const auto digits = [&]
        {
            const auto first = p_;
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
            return p_ - first;
        };
        if (p_ < s_.size() && s_[p_] == '-') ++p_;
        const bool zero = p_ < s_.size() && s_[p_] == '0';
        const auto whole = digits();
        if (whole == 0 || (zero && whole > 1)) return fail();
        if (p_ < s_.size() && s_[p_] == '.') { ++p_; if (digits() == 0) return fail(); }
        if (p_ < s_.size() && (s_[p_] == 'e' || s_[p_] == 'E'))
        {
            ++p_;
            if (p_ < s_.size() && (s_[p_] == '+' || s_[p_] == '-')) ++p_;
            if (digits() == 0) return fail();
        }
        j.kind = Json::Kind::Number;
        j.text = std::string (s_.substr (start, p_ - start));
        return j;
    }
};

// The whole file in one read().
std::optional<std::string> slurp (const fs::path& path)
{
    std::error_code ec;
    const auto size = fs::file_size (path, ec);
    std::ifstream f (path, std::ios::binary);
    if (ec || ! f) return std::nullopt;
    std::string bytes (static_cast<std::size_t> (size), '\0');
    f.read (bytes.data(), static_cast<std::streamsize> (bytes.size()));
    if (static_cast<std::uintmax_t> (f.gcount()) != size) return std::nullopt;
    return bytes;
}
std::optional<Json> readJson (const fs::path& path)
{
    const auto text = slurp (path);
    if (! text) return std::nullopt;
    return JsonReader (*text).document();
}
std::string unsignedText (std::uint64_t n)
{
    std::string s;
    do { s.insert (s.begin(), char ('0' + n % 10)); n /= 10; } while (n != 0);
    return s;
}
std::string integerText (std::int64_t n)
{
    return (n < 0 ? "-" : "") + unsignedText (n < 0 ? std::uint64_t (-(n + 1)) + 1 : std::uint64_t (n));
}
std::string decimalText (const Decimal& d)
{
    auto digits = unsignedText (d.mantissa < 0 ? std::uint64_t (-(d.mantissa + 1)) + 1 : std::uint64_t (d.mantissa));
    if (digits.size() <= d.scale) digits.insert (0, d.scale + 1 - digits.size(), '0');
    const auto point = digits.size() - d.scale;
    return (d.mantissa < 0 || d.negativeZero ? "-" : "") + digits.substr (0, point) + "." + digits.substr (point);
}

// A tagged scalar is an object with exactly "type" and "value", both strings. A table's members are always
// objects or arrays, never bare strings, so the two cannot be confused.
bool isScalar (const Json& j)
{
    if (j.kind != Json::Kind::Object || j.keys.size() != 2) return false;
    const auto* type = j.member ("type");
    const auto* value = j.member ("value");
    return type && value && type->kind == Json::Kind::String && value->kind == Json::Kind::String;
}
std::string tableDiffers (const Table& t, const Json& j, const std::string& at);
// Empty when the value is exactly the expected one; otherwise where and how it differs.
std::string differs (const Value& v, const Json& j, const std::string& at)
{
    if (isScalar (j))
    {
        const auto& type = j.member ("type")->text;
        const auto& want = j.member ("value")->text;
        bool same = false;
        if (const auto* s = std::get_if<std::string> (&v.data)) same = type == "string" && *s == want;
        else if (const auto* i = std::get_if<std::int64_t> (&v.data)) same = type == "integer" && integerText (*i) == want;
        else if (const auto* d = std::get_if<Decimal> (&v.data)) same = type == "decimal" && d->valid() && decimalText (*d) == want;
        else if (const auto* b = std::get_if<bool> (&v.data)) same = type == "bool" && want == (*b ? "true" : "false");
        return same ? std::string() : at + ": expected " + type + " " + want;
    }
    if (j.kind == Json::Kind::Object)
    {
        const auto* t = std::get_if<Table> (&v.data);
        return t ? tableDiffers (*t, j, at) : at + ": expected a table";
    }
    if (j.kind != Json::Kind::Array) return at + ": the expected JSON is neither a value, a table nor an array";
    if (const auto* a = std::get_if<Array> (&v.data))
    {
        if (a->size() != j.items.size()) return at + ": expected " + unsignedText (j.items.size()) + " array items";
        for (std::size_t i = 0; i < a->size(); ++i)
        {
            const auto where = at + "[" + unsignedText (i) + "]";
            if (! isScalar (j.items[i])) return where + ": a scalar array holds scalars";
            if (auto r = differs ((*a)[i], j.items[i], where); ! r.empty()) return r;
        }
        return {};
    }
    if (const auto* a = std::get_if<Tables> (&v.data))
    {
        if (a->empty()) return at + ": an empty array of tables has no TOML spelling; [] is an empty scalar array";
        if (a->size() != j.items.size()) return at + ": expected " + unsignedText (j.items.size()) + " tables";
        for (std::size_t i = 0; i < a->size(); ++i)
        {
            const auto where = at + "[" + unsignedText (i) + "]";
            if (j.items[i].kind != Json::Kind::Object || isScalar (j.items[i])) return where + ": the expected JSON is not a table";
            if (auto r = tableDiffers ((*a)[i], j.items[i], where); ! r.empty()) return r;
        }
        return {};
    }
    return at + ": expected an array";
}
std::string tableDiffers (const Table& t, const Json& j, const std::string& at)
{
    if (t.entries().size() != j.keys.size())
        return at + ": " + unsignedText (t.entries().size()) + " keys, expected " + unsignedText (j.keys.size());
    for (std::size_t i = 0; i < j.keys.size(); ++i)
    {
        const auto where = at + "." + j.keys[i];
        const auto* v = t.find (j.keys[i]);
        if (v == nullptr) return where + ": missing";
        if (auto r = differs (*v, j.items[i], where); ! r.empty()) return r;
    }
    return {};
}
std::string treeDiffers (const ParseResult& result, const Json& expected)
{
    if (const auto* e = std::get_if<Error> (&result))
        return std::string ("refused: ") + codeName (e->code) + " at " + unsignedText (e->line) + ":" + unsignedText (e->column);
    if (expected.kind != Json::Kind::Object || isScalar (expected)) return "the expected root is not a JSON object of keys";
    return tableDiffers (*std::get_if<Table> (&result), expected, "root");
}

bool endsWith (std::string_view s, std::string_view tail)
{
    return s.size() >= tail.size() && s.substr (s.size() - tail.size()) == tail;
}
// The stems of <dir>/*.toml, without the .canonical.toml companions, sorted for a stable report. jsonFiles counts
// the <stem>.json files, not the .positions.json companions.
std::vector<std::string> documents (const fs::path& dir, std::size_t& jsonFiles)
{
    std::vector<std::string> names;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator (dir, ec))
    {
        const auto file = entry.path().filename().string();
        const auto dot = file.rfind ('.');
        if (dot == std::string::npos) continue;
        const auto stem = file.substr (0, dot), extension = file.substr (dot);
        if (extension == ".json" && ! endsWith (stem, ".positions")) ++jsonFiles;
        if (extension == ".toml" && ! endsWith (stem, ".canonical")) names.push_back (stem);
    }
    std::sort (names.begin(), names.end());
    return names;
}
// Empty when the positions file lists exactly the tree's positions, in its depth-first order.
std::string positionsDiffer (const Table& tree, const Json& expected)
{
    const auto actual = fixtures::locate (tree);
    if (expected.kind != Json::Kind::Array) return "not a JSON array";
    if (expected.items.size() != actual.size())
        return unsignedText (actual.size()) + " positions, expected " + unsignedText (expected.items.size());
    const auto pair = [] (const Json* j, std::uint32_t& line, std::uint32_t& column)
    {
        if (j == nullptr || j->kind != Json::Kind::Array || j->items.size() != 2) return false;
        for (const auto* n : { &j->items[0], &j->items[1] })
            if (n->kind != Json::Kind::Number || n->text.empty() || n->text.size() > 9 || n->text.find_first_not_of ("0123456789") != std::string::npos) return false;
        line = std::uint32_t (std::stoul (j->items[0].text)); column = std::uint32_t (std::stoul (j->items[1].text));
        return true;
    };
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        const auto& e = expected.items[i];
        const auto* path = e.member ("path");
        if (e.kind != Json::Kind::Object || path == nullptr || path->kind != Json::Kind::Array) return "entry " + unsignedText (i) + " has no path";
        std::string spelled = "[";
        for (const auto& component : path->items)
            spelled += (spelled.size() == 1 ? "" : ", ") + (component.kind == Json::Kind::String ? fixtures::jsonString (component.text) : component.text);
        spelled += "]";
        const auto where = spelled + ": ";
        if (spelled != actual[i].path) return where + "expected here, the tree has " + actual[i].path;
        std::uint32_t line = 0, column = 0;
        if (! pair (e.member ("at"), line, column) || Position { line, column } != actual[i].at)
            return where + "at " + unsignedText (actual[i].at.line) + ":" + unsignedText (actual[i].at.column);
        const auto* key = e.member ("key");
        if ((key != nullptr) != actual[i].keyed) return where + (actual[i].keyed ? "missing key position" : "an item has no key");
        if (key && (! pair (key, line, column) || Position { line, column } != actual[i].key))
            return where + "key at " + unsignedText (actual[i].key.line) + ":" + unsignedText (actual[i].key.column);
        if (e.keys.size() != (key ? 3u : 2u)) return where + "unexpected members";
    }
    return {};
}
}

int main (int argc, char** argv)
{
    if (argc != 2) { std::printf ("usage: felitronics_toml_corpus_tests <corpus directory>\n"); return 2; }
    const fs::path corpus = argv[1];

    test::group ("valid: the expected tree, the canonical text byte for byte, and the canonical text's tree");
    std::size_t jsonFiles = 0, positioned = 0;
    const auto valid = documents (corpus / "valid", jsonFiles);
    for (const auto& name : valid)
    {
        const auto base = corpus / "valid" / name;
        const auto text = slurp (base.string() + ".toml");
        const auto expected = readJson (base.string() + ".json");
        if (! text || ! expected) { test::ok (false, "valid/" + name + ": unreadable .toml or .json"); continue; }
        const auto result = parse (*text);
        const auto why = treeDiffers (result, *expected);
        test::ok (why.empty(), "valid/" + name + ": " + why);
        const auto* tree = std::get_if<Table> (&result);
        const auto canonicalFile = slurp (base.string() + ".canonical.toml");
        const auto& canonical = canonicalFile ? *canonicalFile : *text;
        test::ok (tree != nullptr && writeChecked (*tree) == canonical, "valid/" + name + ": write() differs from the canonical text");
        if (canonicalFile) test::ok (treeDiffers (parse (canonical), *expected).empty(), "valid/" + name + ": the canonical text's tree differs");
        if (const auto positions = readJson (base.string() + ".positions.json"); positions && tree)
        {
            ++positioned;
            const auto differs = positionsDiffer (*tree, *positions);
            test::ok (differs.empty(), "valid/" + name + ".positions.json: " + differs);
        }
        else test::ok (! fs::exists (base.string() + ".positions.json"), "valid/" + name + ".positions.json: unreadable");
    }
    test::ok (jsonFiles == valid.size(), "valid/: every .json belongs to a .toml");

    test::group ("invalid: exactly the expected code, line and column");
    jsonFiles = 0;
    const auto invalid = documents (corpus / "invalid", jsonFiles);
    for (const auto& name : invalid)
    {
        const auto base = corpus / "invalid" / name;
        const auto text = slurp (base.string() + ".toml");
        const auto expected = readJson (base.string() + ".json");
        const auto* code = expected ? expected->member ("code") : nullptr;
        const auto* line = expected ? expected->member ("line") : nullptr;
        const auto* column = expected ? expected->member ("column") : nullptr;
        if (! text || ! code || ! line || ! column || code->kind != Json::Kind::String
            || line->kind != Json::Kind::Number || column->kind != Json::Kind::Number)
        {
            test::ok (false, "invalid/" + name + ": unreadable .toml, or not {\"code\": string, \"line\": number, \"column\": number}");
            continue;
        }
        const auto result = parse (*text);
        const auto* e = std::get_if<Error> (&result);
        const auto got = e ? std::string (codeName (e->code)) + " " + unsignedText (e->line) + ":" + unsignedText (e->column) : "accepted";
        test::ok (got == code->text + " " + line->text + ":" + column->text,
                  "invalid/" + name + ": expected " + code->text + " " + line->text + ":" + column->text + ", got " + got);
    }
    test::ok (jsonFiles == invalid.size(), "invalid/: every .json belongs to a .toml");
    test::ok (! valid.empty() && ! invalid.empty(), "the corpus was found and is not empty");

    std::printf ("corpus: %s valid documents (%s with positions) and %s invalid documents\n", unsignedText (valid.size()).c_str(),
                 unsignedText (positioned).c_str(), unsignedText (invalid.size()).c_str());
    return test::report();
}

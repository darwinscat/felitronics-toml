// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml: https://github.com/darwinscat/felitronics-toml

// felitronics::toml: a strict, deterministic subset of TOML 1.0, a parser and a canonical writer.
// Header-only C++20 with no dependencies beyond the standard library. The contract: docs/TOML-SUBSET.md.

#pragma once

#include <algorithm>
#include <cfloat>      // FLT_EVAL_METHOD
#include <cmath>       // signbit preserves negative zero without assuming byte order
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace felitronics::toml
{

// Offline, allocating document I/O. No libc number conversion, locale, exceptions or RTTI.
// Comments are intentionally discarded; anything that must survive a save belongs in a string value.
// noexcept describes error reporting, not recovery from process-wide allocation failure.
inline constexpr std::size_t kMaxDocument = 1024 * 1024;
inline constexpr std::size_t kMaxDepth = 16;
inline constexpr std::size_t kMaxKey = 256;
inline constexpr std::size_t kMaxString = 65536;
inline constexpr std::size_t kMaxArray = 65536;
inline constexpr std::size_t kMaxEntries = 65536;

namespace detail
{
[[nodiscard]] inline std::uint64_t power10 (std::uint8_t scale) noexcept
{
    std::uint64_t n = 1;
    for (std::uint8_t i = 0; i < scale; ++i) n *= 10;
    return n;
}
[[nodiscard]] inline std::uint64_t magnitude (std::int64_t n) noexcept
{
    return n < 0 ? std::uint64_t (-(n + 1)) + 1 : std::uint64_t (n);
}
}

struct Decimal
{
    std::int64_t mantissa = 0;
    std::uint8_t scale = 1;
    bool negativeZero = false;       // meaningful only for a zero mantissa
    static constexpr std::int64_t kMaxMantissa = std::int64_t (1) << 53;

    [[nodiscard]] bool valid() const noexcept
    {
        return scale >= 1 && scale <= 9 && mantissa >= -kMaxMantissa && mantissa <= kMaxMantissa
            && (! negativeZero || mantissa == 0);
    }

    // Both integers are exact binary64 values. A single IEEE division, with round-to-nearest/ties-to-even,
    // therefore rounds the exact rational correctly: the same bits as a correctly rounded strtod of the
    // decimal text, including -0.0. Fast-math and a changed floating-point rounding mode are outside the contract.
    [[nodiscard]] double toDouble() const noexcept
    {
        if (! valid()) return std::numeric_limits<double>::quiet_NaN();
        if (negativeZero) return -0.0;
        return double (mantissa) / double (detail::power10 (scale));
    }

    // Refusal is {0, 0, false}, for which valid() is false. Round the binary64 PRODUCT x * 10^scale
    // half away from zero; the product itself can round, so this is not a decimal-arithmetic oracle for x.
    // Subtract the truncated integer before testing the half: adding 0.5 first would misround just below it.
    [[nodiscard]] static Decimal fromDouble (double x, std::uint8_t scale) noexcept
    {
        if (scale < 1 || scale > 9) return { 0, 0, false };
        const double product = x * double (detail::power10 (scale));
        const double a = std::signbit (product) ? -product : product;
        if (! (a >= 0.0 && a <= double (kMaxMantissa))) return { 0, 0, false };
        auto n = std::int64_t (a);
        if (a - double (n) >= 0.5) ++n;
        if (n > kMaxMantissa) return { 0, 0, false };
        return { std::signbit (x) ? -n : n, scale, n == 0 && std::signbit (x) };
    }
    bool operator== (const Decimal&) const = default;
};
static_assert (std::numeric_limits<double>::is_iec559 && std::numeric_limits<double>::digits == 53);
// x87 arithmetic (FLT_EVAL_METHOD 2) rounds the division to 64 bits first and then to 53, which is not always the
// correctly rounded result: Decimal{7832510068573872, 8} comes out one unit in the last place low.
static_assert (FLT_EVAL_METHOD == 0 || FLT_EVAL_METHOD == 1,
               "felitronics::toml needs double arithmetic evaluated as double: on 32-bit x86, build with SSE2 "
               "(-msse2 -mfpmath=sse; MSVC's default)");

// Where something was written: a 1-based line and a 1-based column counted in code points, as in Error, and
// the source number the caller passed to parse(), which tells documents apart once overlay() has merged them.
// Line 0 means "not read from a document": a value the caller built. Positions never take part in equality.
struct Position
{
    std::uint32_t line = 0;
    std::uint32_t column = 0;
    std::uint32_t source = 0;
    bool operator== (const Position&) const = default;
};

struct Value;
struct Entry;
namespace detail { class Parser; }

// Keys cannot be changed in place: that keeps the lookup index consistent with insertion order.
// The sorted AVL index stores vector offsets, so lookup and insertion take O(log n) comparisons even for
// hostile key sets. Reallocation moves no pointer retained by the index. The entries remain in caller order.
class Table
{
public:
    Table();
    ~Table();
    Table (const Table&);
    Table (Table&&) noexcept;
    Table& operator= (const Table&);
    Table& operator= (Table&&) noexcept;
    [[nodiscard]] const std::vector<Entry>& entries() const noexcept;
    [[nodiscard]] const Value* find (std::string_view key) const noexcept;
    [[nodiscard]] Value* find (std::string_view key) noexcept;
    // The whole entry: its key, its value and where the key was written. Null for a missing key.
    [[nodiscard]] const Entry* entry (std::string_view key) const noexcept;
    [[nodiscard]] bool insert (std::string key, Value value, Position keyPosition = {});
    // Value equality ignores mapping order, as TOML does. entries() retains order for schema presentation.
    [[nodiscard]] bool operator== (const Table& other) const;

    // Where the table was defined: its [header], the { of an inline table, or the key that implied it. The root
    // of a parsed document is at 1:1. Not part of equality.
    Position position {};

private:
    struct Node { std::size_t left = 0, right = 0; int height = 1; };
    std::vector<Entry> entries_;
    std::vector<Node> index_;     // one-based links; zero is the empty subtree
    std::size_t root_ = 0;
    enum class Origin { Implicit, Dotted, Header };
    Origin origin_ = Origin::Implicit; // parser bookkeeping, excluded from value equality
    [[nodiscard]] int height (std::size_t n) const noexcept;
    void refresh (std::size_t n) noexcept;
    [[nodiscard]] std::size_t rotate (std::size_t n, bool left) noexcept;
    [[nodiscard]] std::size_t link (std::size_t n, std::size_t added) noexcept;
    [[nodiscard]] std::size_t locate (std::string_view key) const noexcept;
    friend class detail::Parser;
};

using Array = std::vector<Value>;
using Tables = std::vector<Table>;
struct Value
{
    using Data = std::variant<std::string, std::int64_t, Decimal, bool, Array, Table, Tables>;
    Data data;
    // Where the value starts in its document: the quote, the first digit or sign, the t or f, the [ of an array.
    // A table value has its table's position. Line 0 for a value the caller built. Not part of equality.
    Position position {};
    Value() : data (std::string{}) {}
    Value (std::string x) : data (std::move (x)) {}
    Value (const char* x) : data (std::string (x)) {}
    Value (std::int64_t x) : data (x) {}
    Value (Decimal x) : data (x) {}
    Value (bool x) : data (x) {}
    Value (Array x) : data (std::move (x)) {}
    Value (Table x) : data (std::move (x)), position (std::get_if<Table> (&data)->position) {}
    Value (Tables x) : data (std::move (x)) {}
    bool operator== (const Value& other) const { return data == other.data; }
};
struct Entry
{
    std::string key;
    Value value;
    Position keyPosition {};   // where the key was first written: the first character of its bare or quoted spelling
};
inline Table::Table() = default;
inline Table::~Table() = default;
inline Table::Table (const Table&) = default;
inline Table::Table (Table&& other) noexcept
    : position (other.position), entries_ (std::move (other.entries_)), index_ (std::move (other.index_)),
      root_ (std::exchange (other.root_, 0)), origin_ (other.origin_) {}
inline Table& Table::operator= (const Table&) = default;
inline Table& Table::operator= (Table&& other) noexcept
{
    if (this != &other)
    {
        position = other.position;
        entries_ = std::move (other.entries_); index_ = std::move (other.index_);
        root_ = std::exchange (other.root_, 0); origin_ = other.origin_;
    }
    return *this;
}
inline const std::vector<Entry>& Table::entries() const noexcept { return entries_; }
inline int Table::height (std::size_t n) const noexcept { return n == 0 ? 0 : index_[n - 1].height; }
inline void Table::refresh (std::size_t n) noexcept
{
    auto& x = index_[n - 1];
    x.height = 1 + std::max (height (x.left), height (x.right));
}
inline std::size_t Table::rotate (std::size_t n, bool left) noexcept
{
    auto& x = index_[n - 1];
    const auto pivot = left ? x.right : x.left;
    auto& y = index_[pivot - 1];
    if (left) { x.right = y.left; y.left = n; }
    else      { x.left = y.right; y.right = n; }
    refresh (n); refresh (pivot);
    return pivot;
}
inline std::size_t Table::link (std::size_t n, std::size_t added) noexcept
{
    if (n == 0) return added;
    auto& x = index_[n - 1];
    if (entries_[added - 1].key < entries_[n - 1].key) x.left = link (x.left, added);
    else x.right = link (x.right, added);
    refresh (n);
    if (height (x.left) - height (x.right) > 1)
    {
        const auto& child = index_[x.left - 1];
        if (height (child.left) < height (child.right)) x.left = rotate (x.left, true);
        return rotate (n, false);
    }
    if (height (x.right) - height (x.left) > 1)
    {
        const auto& child = index_[x.right - 1];
        if (height (child.right) < height (child.left)) x.right = rotate (x.right, false);
        return rotate (n, true);
    }
    return n;
}
inline std::size_t Table::locate (std::string_view key) const noexcept
{
    auto n = root_;
    while (n != 0)
    {
        const int c = key.compare (entries_[n - 1].key);
        if (c == 0) break;
        n = c < 0 ? index_[n - 1].left : index_[n - 1].right;
    }
    return n;
}
inline const Value* Table::find (std::string_view key) const noexcept
{
    const auto n = locate (key);
    return n == 0 ? nullptr : &entries_[n - 1].value;
}
inline Value* Table::find (std::string_view key) noexcept
{
    const auto n = locate (key);
    return n == 0 ? nullptr : &entries_[n - 1].value;
}
inline const Entry* Table::entry (std::string_view key) const noexcept
{
    const auto n = locate (key);
    return n == 0 ? nullptr : &entries_[n - 1];
}
inline bool Table::insert (std::string key, Value value, Position keyPosition)
{
    if (locate (key) != 0) return false;
    entries_.push_back ({ std::move (key), std::move (value), keyPosition });
    index_.push_back ({});
    root_ = link (root_, entries_.size());
    return true;
}
inline bool Table::operator== (const Table& other) const
{
    if (entries_.size() != other.entries_.size()) return false;
    for (const auto& e : entries_)
    {
        const auto* v = other.find (e.key);
        if (v == nullptr || ! (e.value == *v)) return false;
    }
    return true;
}

enum class Code
{
    DocumentLimit, CanonicalLimit, Bom, InvalidUtf8, InvalidControl, BareCarriageReturn,
    ExpectedKey, KeyLimit, DepthLimit, ExpectedEquals, ExpectedValue, UnsupportedValue,
    UnterminatedString, StringLimit, InvalidEscape, InvalidUnicodeEscape, InvalidNumber, IntegerRange,
    DecimalScale, DecimalRange, ExpectedArraySeparator, UnterminatedArray, MixedArray, ArrayLimit,
    ExpectedHeaderEnd, TrailingCharacters, DuplicateKey, RedefinedTable, TableValueConflict, EntryLimit
};
struct Error
{
    Code code;
    std::uint32_t line;
    std::uint32_t column;       // one-based, in code points (a tab and a 4-byte emoji are one column each);
                                // EOF is one column past the last character
    bool operator== (const Error&) const = default;
};
using ParseResult = std::variant<Table, Error>;
[[nodiscard]] inline const char* codeName (Code code) noexcept
{
    switch (code)
    {
#define FELITRONICS_TOML_CODE(name) case Code::name: return #name;
        FELITRONICS_TOML_CODE(DocumentLimit) FELITRONICS_TOML_CODE(CanonicalLimit) FELITRONICS_TOML_CODE(Bom)
        FELITRONICS_TOML_CODE(InvalidUtf8) FELITRONICS_TOML_CODE(InvalidControl)
        FELITRONICS_TOML_CODE(BareCarriageReturn) FELITRONICS_TOML_CODE(ExpectedKey)
        FELITRONICS_TOML_CODE(KeyLimit) FELITRONICS_TOML_CODE(DepthLimit)
        FELITRONICS_TOML_CODE(ExpectedEquals) FELITRONICS_TOML_CODE(ExpectedValue)
        FELITRONICS_TOML_CODE(UnsupportedValue) FELITRONICS_TOML_CODE(UnterminatedString) FELITRONICS_TOML_CODE(StringLimit)
        FELITRONICS_TOML_CODE(InvalidEscape) FELITRONICS_TOML_CODE(InvalidUnicodeEscape)
        FELITRONICS_TOML_CODE(InvalidNumber) FELITRONICS_TOML_CODE(IntegerRange)
        FELITRONICS_TOML_CODE(DecimalScale) FELITRONICS_TOML_CODE(DecimalRange)
        FELITRONICS_TOML_CODE(ExpectedArraySeparator) FELITRONICS_TOML_CODE(UnterminatedArray)
        FELITRONICS_TOML_CODE(MixedArray) FELITRONICS_TOML_CODE(ArrayLimit)
        FELITRONICS_TOML_CODE(ExpectedHeaderEnd) FELITRONICS_TOML_CODE(TrailingCharacters)
        FELITRONICS_TOML_CODE(DuplicateKey) FELITRONICS_TOML_CODE(RedefinedTable)
        FELITRONICS_TOML_CODE(TableValueConflict) FELITRONICS_TOML_CODE(EntryLimit)
#undef FELITRONICS_TOML_CODE
    }
    return "Unknown";
}

namespace detail
{
[[nodiscard]] inline bool bare (char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
}
[[nodiscard]] inline bool digit (char c) noexcept { return c >= '0' && c <= '9'; }
[[nodiscard]] inline int hex (char c) noexcept
{
    return digit (c) ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}
// Zero means an invalid sequence. Reject overlong forms, surrogate code points and values above U+10FFFF.
[[nodiscard]] inline std::size_t utf8Width (std::string_view s, std::size_t p) noexcept
{
    const auto first = static_cast<unsigned char> (s[p]);
    if (first < 0x80) return 1;
    const std::size_t n = first >= 0xC2 && first <= 0xDF ? 2 : first >= 0xE0 && first <= 0xEF ? 3
                       : first >= 0xF0 && first <= 0xF4 ? 4 : 0;
    if (n == 0 || n > s.size() - p) return 0;
    std::uint32_t scalar = first & (0x7Fu >> n);
    for (std::size_t i = 1; i < n; ++i)
    {
        const auto b = static_cast<unsigned char> (s[p + i]);
        if ((b & 0xC0u) != 0x80u) return 0;
        scalar = (scalar << 6) | (b & 0x3Fu);
    }
    if ((n == 2 && scalar < 0x80) || (n == 3 && scalar < 0x800) || (n == 4 && scalar < 0x10000)
        || (scalar >= 0xD800 && scalar <= 0xDFFF) || scalar > 0x10FFFF) return 0;
    return n;
}
// The position of byte offset p. Only LF ends a line. A column counts code points: every byte that is not a
// UTF-8 continuation byte (10xxxxxx) starts one. For valid UTF-8 that is the code point index, and it stays
// defined for DocumentLimit, whose text is never decoded. Every other error points at the start of a character.
[[nodiscard]] inline Error errorAt (Code code, std::string_view text, std::size_t p) noexcept
{
    std::uint32_t line = 1, column = 1;
    for (std::size_t i = 0; i < p; ++i)
    {
        if (text[i] == '\n') { ++line; column = 1; }
        else if ((static_cast<unsigned char> (text[i]) & 0xC0u) != 0x80u) ++column;
    }
    return { code, line, column };
}
inline void appendUtf8 (std::string& s, std::uint32_t u)
{
    if (u < 0x80) s += char (u);
    else if (u < 0x800) { s += char (0xC0u | (u >> 6)); s += char (0x80u | (u & 63u)); }
    else if (u < 0x10000)
    { s += char (0xE0u | (u >> 12)); s += char (0x80u | ((u >> 6) & 63u)); s += char (0x80u | (u & 63u)); }
    else
    { s += char (0xF0u | (u >> 18)); s += char (0x80u | ((u >> 12) & 63u));
      s += char (0x80u | ((u >> 6) & 63u)); s += char (0x80u | (u & 63u)); }
}

class Parser
{
public:
    Parser (std::string_view s, std::uint32_t source) : text_ (s), source_ (source) {}
    [[nodiscard]] ParseResult run()
    {
        if (text_.size() > kMaxDocument) return errorAt (Code::DocumentLimit, kMaxDocument);
        scanEncoding();
        end_ = lexical_ ? lexical_->second : text_.size();
        Table root;
        root.position = position (0);
        Table* current = &root;
        std::size_t currentDepth = 0;
        while (! failed_)
        {
            spaceLines();
            if (atEnd()) break;
            if (peek() == '[')
            {
                const auto where = position (pos_);
                ++pos_;
                const bool array = peek() == '[';
                if (array) ++pos_;
                auto path = keys (0);
                if (failed_) break;
                if (peek() != ']') { fail (Code::ExpectedHeaderEnd, pos_); break; }
                ++pos_;
                if (array)
                {
                    if (peek() != ']') { fail (Code::ExpectedHeaderEnd, pos_); break; }
                    ++pos_;
                }
                if (! lineEnd()) break;
                current = header (root, path, array, where);
                currentDepth = path.size();
            }
            else
            {
                auto path = keys (currentDepth);
                if (failed_) break;
                if (peek() != '=') { fail (Code::ExpectedEquals, pos_); break; }
                ++pos_; spaces();
                Value value = readValue();
                if (failed_ || ! lineEnd()) break;
                assign (*current, path, std::move (value));
            }
        }
        if (lexical_ && (! failed_ || lexical_->second <= failurePos_))
            return errorAt (lexical_->first, lexical_->second);
        if (failed_) return errorAt (failureCode_, failurePos_);
        return root;
    }

private:
    struct Key { std::string name; std::size_t pos; Position position; };
    std::string_view text_;
    std::uint32_t source_ = 0;
    std::size_t pos_ = 0, end_ = 0, entries_ = 0, failurePos_ = 0;
    Code failureCode_ = Code::ExpectedValue;
    bool failed_ = false;
    std::optional<std::pair<Code, std::size_t>> lexical_;
    // The position of byte offset p, counted as errorAt counts it. The parser asks in increasing order, so a
    // cursor that only moves forward makes every position of a document cost O(size) in total.
    std::size_t cursor_ = 0;
    std::uint32_t cursorLine_ = 1, cursorColumn_ = 1;
    [[nodiscard]] Position position (std::size_t p) noexcept
    {
        if (p < cursor_) { cursor_ = 0; cursorLine_ = 1; cursorColumn_ = 1; }
        for (; cursor_ < p; ++cursor_)
        {
            if (text_[cursor_] == '\n') { ++cursorLine_; cursorColumn_ = 1; }
            else if ((static_cast<unsigned char> (text_[cursor_]) & 0xC0u) != 0x80u) ++cursorColumn_;
        }
        return { cursorLine_, cursorColumn_, source_ };
    }
    [[nodiscard]] Error errorAt (Code code, std::size_t p) const noexcept { return detail::errorAt (code, text_, p); }
    void scanEncoding()
    {
        if (text_.substr (0, 3) == "\xEF\xBB\xBF") { lexical_ = { Code::Bom, 0 }; return; }
        for (std::size_t i = 0; i < text_.size();)
        {
            const auto c = static_cast<unsigned char> (text_[i]);
            if (c == '\r' && (i + 1 == text_.size() || text_[i + 1] != '\n'))
            { lexical_ = { Code::BareCarriageReturn, i }; return; }
            if ((c < 0x20 && c != '\t' && c != '\n' && c != '\r') || c == 0x7F)
            { lexical_ = { Code::InvalidControl, i }; return; }
            const auto n = utf8Width (text_, i);
            if (n == 0) { lexical_ = { Code::InvalidUtf8, i }; return; }
            i += n;
        }
    }
    void fail (Code code, std::size_t p)
    {
        if (! failed_) { failed_ = true; failureCode_ = code; failurePos_ = p; }
    }
    [[nodiscard]] bool atEnd() const noexcept { return pos_ >= end_; }
    [[nodiscard]] char peek() const noexcept { return atEnd() ? '\0' : text_[pos_]; }
    void spaces() { while (peek() == ' ' || peek() == '\t') ++pos_; }
    void comment() { if (peek() == '#') while (! atEnd() && peek() != '\n' && peek() != '\r') ++pos_; }
    void spaceLines()
    {
        for (;;)
        {
            spaces(); comment();
            if (peek() == '\r' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '\n') pos_ += 2;
            else if (peek() == '\n') ++pos_;
            else break;
        }
    }
    [[nodiscard]] bool lineEnd()
    {
        spaces(); comment();
        if (atEnd() || peek() == '\n' || (peek() == '\r' && pos_ + 1 < text_.size() && text_[pos_ + 1] == '\n')) return true;
        fail (Code::TrailingCharacters, pos_); return false;
    }
    [[nodiscard]] std::string string (bool key)
    {
        std::string out;
        ++pos_;
        while (! atEnd() && ! failed_)
        {
            const auto start = pos_;
            char c = text_[pos_++];
            if (c == '"') return out;
            if (c == '\n' || c == '\r') { fail (Code::UnterminatedString, start); break; }
            if (c == '\\')
            {
                if (atEnd()) { fail (Code::UnterminatedString, pos_); break; }
                c = text_[pos_++];
                switch (c)
                {
                    case '"': case '\\': out += c; break;
                    case 'b': out += '\b'; break;
                    case 't': out += '\t'; break;
                    case 'n': out += '\n'; break;
                    case 'f': out += '\f'; break;
                    case 'r': out += '\r'; break;
                    case 'u': case 'U':
                    {
                        std::uint32_t u = 0;
                        const int n = c == 'u' ? 4 : 8;
                        for (int i = 0; i < n; ++i)
                        {
                            const int h = hex (peek());
                            if (h < 0) { fail (Code::InvalidUnicodeEscape, pos_); break; }
                            u = (u << 4) | std::uint32_t (h); ++pos_;
                        }
                        if (! failed_)
                        {
                            if (u > 0x10FFFF || (u >= 0xD800 && u <= 0xDFFF)) fail (Code::InvalidUnicodeEscape, start);
                            else appendUtf8 (out, u);
                        }
                        break;
                    }
                    default: fail (Code::InvalidEscape, pos_ - 1); break;
                }
            }
            else
            {
                // A whole UTF-8 sequence at a time, so a limit error points at a character, never inside one.
                // Every sequence before end_ was validated by scanEncoding; zero cannot happen, and must not loop.
                const auto n = utf8Width (text_, start);
                if (n == 0) { fail (Code::InvalidUtf8, start); break; }
                out.append (text_.substr (start, n));
                pos_ = start + n;
            }
            if (out.size() > (key ? kMaxKey : kMaxString)) fail (key ? Code::KeyLimit : Code::StringLimit, start);
        }
        if (! failed_) fail (Code::UnterminatedString, pos_);
        return out;
    }
    [[nodiscard]] std::vector<Key> keys (std::size_t base)
    {
        std::vector<Key> path;
        for (;;)
        {
            spaces();
            if (base + path.size() == kMaxDepth) { fail (Code::DepthLimit, pos_); break; }
            const auto start = pos_;
            std::string name;
            if (peek() == '"') name = string (true);
            else
            {
                while (bare (peek()))
                {
                    if (name.size() == kMaxKey) { fail (Code::KeyLimit, pos_); break; }
                    name += text_[pos_++];
                }
                if (name.empty()) fail (Code::ExpectedKey, pos_);
            }
            if (failed_) break;
            path.push_back ({ std::move (name), start, position (start) });
            spaces();
            if (peek() != '.') break;
            ++pos_;
        }
        return path;
    }
    [[nodiscard]] Value scalar()
    {
        const auto start = pos_;
        if (peek() == '"') return string (false);
        if (atEnd() || peek() == '#' || peek() == '\n' || peek() == '\r' || peek() == ',' || peek() == ']')
        { fail (Code::ExpectedValue, pos_); return {}; }
        if (! digit (peek()) && peek() != '+' && peek() != '-' && peek() != 't' && peek() != 'f')
        { fail (Code::UnsupportedValue, pos_); return {}; }
        while (! atEnd() && peek() != ' ' && peek() != '\t' && peek() != '\r' && peek() != '\n'
               && peek() != ',' && peek() != ']' && peek() != '#') ++pos_;
        if (lexical_ && pos_ == end_)
        { fail (lexical_->first, end_); return {}; }
        const auto token = text_.substr (start, pos_ - start);
        if (token == "true") return true;
        if (token == "false") return false;
        std::size_t p = 0;
        const bool negative = token[p] == '-';
        if (token[p] == '-' || token[p] == '+') ++p;
        const auto wholeStart = p;
        while (p < token.size() && digit (token[p])) ++p;
        if (p == wholeStart || (p - wholeStart > 1 && token[wholeStart] == '0'))
        { fail (Code::InvalidNumber, start); return {}; }
        const auto wholeEnd = p;
        std::uint8_t scale = 0;
        if (p < token.size() && token[p] == '.')
        {
            ++p;
            while (p < token.size() && digit (token[p]))
            {
                if (scale == 9) { fail (Code::DecimalScale, start + p); return {}; }
                ++scale; ++p;
            }
            if (scale == 0) { fail (Code::InvalidNumber, start); return {}; }
        }
        if (p != token.size()) { fail (Code::InvalidNumber, start); return {}; }
        const std::uint64_t limit = scale != 0 ? std::uint64_t (Decimal::kMaxMantissa)
            : std::uint64_t (std::numeric_limits<std::int64_t>::max()) + (negative ? 1u : 0u);
        std::uint64_t n = 0;
        for (std::size_t i = wholeStart; i < token.size(); ++i)
        {
            if (i == wholeEnd && scale != 0) continue;
            const auto d = std::uint64_t (token[i] - '0');
            if (n > (limit - d) / 10)
            { fail (scale != 0 ? Code::DecimalRange : Code::IntegerRange, start); return {}; }
            n = n * 10 + d;
        }
        const auto signedN = negative ? (n == 0 ? std::int64_t (0) : -std::int64_t (n - 1) - 1) : std::int64_t (n);
        if (scale != 0) return Decimal { signedN, scale, negative && n == 0 };
        return signedN;
    }
    [[nodiscard]] Value readValue()
    {
        const auto where = position (pos_);
        if (peek() != '[')
        {
            Value v = scalar();
            v.position = where;
            return v;
        }
        ++pos_; spaceLines();
        Value result { Array{} };
        result.position = where;
        auto& a = *std::get_if<Array> (&result.data);
        if (peek() == ']') { ++pos_; return result; }
        for (;;)
        {
            if (atEnd()) { fail (Code::UnterminatedArray, pos_); break; }
            if (a.size() == kMaxArray) { fail (Code::ArrayLimit, pos_); break; }
            const auto start = pos_;
            const auto itemWhere = position (start);
            Value v = scalar();
            if (failed_) break;
            v.position = itemWhere;
            if (! a.empty() && v.data.index() != a.front().data.index()) { fail (Code::MixedArray, start); break; }
            a.push_back (std::move (v));
            spaceLines();
            if (peek() == ']') { ++pos_; break; }
            if (atEnd()) { fail (Code::UnterminatedArray, pos_); break; }
            if (peek() != ',') { fail (Code::ExpectedArraySeparator, pos_); break; }
            ++pos_; spaceLines();
            if (peek() == ']') { ++pos_; break; }
        }
        return result;
    }
    [[nodiscard]] bool count (std::size_t p)
    {
        if (entries_ == kMaxEntries) { fail (Code::EntryLimit, p); return false; }
        ++entries_; return true;
    }
    [[nodiscard]] Value* add (Table& t, const Key& k, Value v)
    {
        if (! count (k.pos)) return nullptr;
        if (! t.insert (k.name, std::move (v), k.position)) { fail (Code::DuplicateKey, k.pos); return nullptr; }
        return t.find (k.name);
    }
    [[nodiscard]] Table* descend (Table& t, const Key& k, bool dotted)
    {
        auto* v = t.find (k.name);
        if (v == nullptr)
        {
            // An implicit or dotted table is where its key first names it, until a header defines it.
            Table child;
            child.origin_ = dotted ? Table::Origin::Dotted : Table::Origin::Implicit;
            child.position = k.position;
            v = add (t, k, std::move (child));
            if (v == nullptr) return nullptr;
        }
        if (auto* child = std::get_if<Table> (&v->data))
        {
            if (dotted && child->origin_ == Table::Origin::Header)
            { fail (Code::RedefinedTable, k.pos); return nullptr; }
            if (dotted) child->origin_ = Table::Origin::Dotted;
            return child;
        }
        if (! dotted)
            if (auto* a = std::get_if<Tables> (&v->data); a != nullptr && ! a->empty()) return &a->back();
        fail (Code::TableValueConflict, k.pos); return nullptr;
    }
    void assign (Table& t, const std::vector<Key>& path, Value v)
    {
        Table* parent = &t;
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
            parent = descend (*parent, path[i], true);
            if (parent == nullptr) return;
        }
        const auto& key = path.back();
        if (const auto* old = parent->find (key.name))
        {
            fail (old->data.index() >= 5 ? Code::TableValueConflict : Code::DuplicateKey, key.pos); return;
        }
        (void) add (*parent, key, std::move (v));
    }
    [[nodiscard]] Table* header (Table& root, const std::vector<Key>& path, bool array, Position where)
    {
        Table* parent = &root;
        for (std::size_t i = 0; i + 1 < path.size(); ++i)
        {
            parent = descend (*parent, path[i], false);
            if (parent == nullptr) return nullptr;
        }
        const auto& key = path.back();
        auto* v = parent->find (key.name);
        if (array)
        {
            if (v == nullptr)
            {
                Value container { Tables{} };
                container.position = where;     // the array of tables is where its first [[header]] is
                v = add (*parent, key, std::move (container));
            }
            if (v == nullptr) return nullptr;
            if (auto* a = std::get_if<Tables> (&v->data))
            {
                if (a->size() == kMaxArray) { fail (Code::ArrayLimit, key.pos); return nullptr; }
                if (! count (key.pos)) return nullptr;
                a->emplace_back();
                a->back().origin_ = Table::Origin::Header;
                a->back().position = where;
                return &a->back();
            }
        }
        else
        {
            if (v == nullptr) v = add (*parent, key, Table{});
            if (v == nullptr) return nullptr;
            if (auto* child = std::get_if<Table> (&v->data))
            {
                if (child->origin_ != Table::Origin::Implicit) { fail (Code::RedefinedTable, key.pos); return nullptr; }
                child->origin_ = Table::Origin::Header;
                child->position = v->position = where;
                return child;
            }
        }
        fail (Code::TableValueConflict, key.pos); return nullptr;
    }
};
} // namespace detail


namespace detail
{
inline void unsignedText (std::string& out, std::uint64_t n)
{
    char digits[20];
    std::size_t size = 0;
    do { digits[size++] = char ('0' + n % 10); n /= 10; } while (n != 0);
    while (size != 0) out += digits[--size];
}
[[nodiscard]] inline bool validString (std::string_view s, std::size_t limit)
{
    if (s.size() > limit) return false;
    for (std::size_t i = 0; i < s.size();)
    {
        const auto n = utf8Width (s, i);
        if (n == 0) return false;
        i += n;
    }
    return true;
}
inline void quoted (std::string& out, std::string_view s)
{
    out += '"';
    for (const char c : s)
    {
        switch (c)
        {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\n': out += "\\n"; break;
            case '\f': out += "\\f"; break;
            case '\r': out += "\\r"; break;
            default:
            {
                const auto u = static_cast<unsigned char> (c);
                if ((u < 0x20 && u != '\t') || u == 0x7F)
                {
                    out += "\\u00";
                    out += "0123456789ABCDEF"[u >> 4]; out += "0123456789ABCDEF"[u & 15u];
                }
                else out += c;
                break;
            }
        }
    }
    out += '"';
}
inline void keyText (std::string& out, std::string_view key)
{
    if (! key.empty() && std::all_of (key.begin(), key.end(), bare)) out += key;
    else quoted (out, key);
}

class Writer
{
public:
    [[nodiscard]] std::optional<std::string> run (const Table& root)
    {
        table (root, "", 0);
        if (failed_) return std::nullopt;
        return std::move (out_);
    }
private:
    std::string out_;
    std::size_t entries_ = 0;
    bool failed_ = false;
    void bound() { if (out_.size() > kMaxDocument) failed_ = true; }
    void count() { if (++entries_ > kMaxEntries) failed_ = true; }
    void scalar (const Value& value)
    {
        if (const auto* s = std::get_if<std::string> (&value.data))
        {
            if (! validString (*s, kMaxString)) { failed_ = true; return; }
            quoted (out_, *s);
        }
        else if (const auto* n = std::get_if<std::int64_t> (&value.data))
        {
            if (*n < 0) out_ += '-';
            unsignedText (out_, magnitude (*n));
        }
        else if (const auto* d = std::get_if<Decimal> (&value.data))
        {
            if (! d->valid()) { failed_ = true; return; }
            if (d->mantissa < 0 || d->negativeZero) out_ += '-';
            const auto absolute = magnitude (d->mantissa), power = power10 (d->scale);
            unsignedText (out_, absolute / power);
            out_ += '.';
            auto fraction = absolute % power;
            for (auto place = power / 10; place != 0; place /= 10)
            {
                out_ += char ('0' + fraction / place);
                fraction %= place;
            }
        }
        else if (const auto* b = std::get_if<bool> (&value.data)) out_ += *b ? "true" : "false";
        else failed_ = true;
        bound();
    }
    void value (const Value& v)
    {
        if (const auto* a = std::get_if<Array> (&v.data))
        {
            if (a->size() > kMaxArray) { failed_ = true; return; }
            out_ += '[';
            for (std::size_t i = 0; i < a->size() && ! failed_; ++i)
            {
                if ((*a)[i].data.index() != a->front().data.index()) { failed_ = true; return; }
                if (i != 0) out_ += ", ";
                scalar ((*a)[i]);
            }
            out_ += ']';
        }
        else scalar (v);
    }
    void heading (const std::string& path, bool array)
    {
        if (! out_.empty()) out_ += '\n';
        out_ += array ? "[[" : "[";
        out_ += path;
        out_ += array ? "]]\n" : "]\n";
        bound();
    }
    void table (const Table& t, const std::string& path, std::size_t depth)
    {
        // Checking before descent caps call-stack depth even for an invalid caller-built tree.
        if (depth > kMaxDepth) { failed_ = true; return; }
        for (const auto& e : t.entries())
        {
            count();
            if (depth == kMaxDepth || ! validString (e.key, kMaxKey)) failed_ = true;
            if (failed_) return;
            if (e.value.data.index() < 5)
            {
                keyText (out_, e.key); out_ += " = "; value (e.value); out_ += '\n'; bound();
                if (failed_) return;
            }
        }
        // Grouping is required by TOML's current-table rule. Order within each group is caller order.
        for (int group = 0; group < 2; ++group)
            for (const auto& e : t.entries())
            {
                if (failed_) return;
                const auto* child = std::get_if<Table> (&e.value.data);
                const auto* array = std::get_if<Tables> (&e.value.data);
                if ((group == 0 && child == nullptr) || (group == 1 && array == nullptr)) continue;
                std::string next = path;
                if (depth != 0) next += '.';
                keyText (next, e.key);
                if (group == 0) { heading (next, false); table (*child, next, depth + 1); }
                else
                {
                    if (array->empty() || array->size() > kMaxArray) { failed_ = true; return; }
                    for (const auto& element : *array)
                    {
                        count();
                        if (failed_) return;
                        heading (next, true); table (element, next, depth + 1);
                        if (failed_) return;
                    }
                }
            }
    }
};
} // namespace detail

// Invalid caller-built trees return nullopt: mixed/nested arrays, empty Tables (use Array{} for []), invalid
// UTF-8/decimals or any exceeded limit. An empty root is valid and produces the empty string.
[[nodiscard]] inline std::optional<std::string> writeChecked (const Table& root)
{
    return detail::Writer{}.run (root);
}
// Convenience for a known-valid tree. Empty on refusal; writeChecked distinguishes refusal from an empty root.
[[nodiscard]] inline std::string write (const Table& root)
{
    auto out = writeChecked (root);
    return out ? std::move (*out) : std::string{};
}
// source is copied into every Position of the tree, so values from different documents stay apart after overlay().
[[nodiscard]] inline ParseResult parse (std::string_view text, std::uint32_t source = 0) noexcept
{
    auto result = detail::Parser (text, source).run();
    if (const auto* root = std::get_if<Table> (&result); root != nullptr && ! writeChecked (*root))
    {
        // Canonical spacing/escapes can grow a compact input. Refuse it here so EVERY successful parse can
        // be written and reparsed under the same 1 MiB limit. This resource check follows syntax validation.
        return detail::errorAt (Code::CanonicalLimit, text, text.size());
    }
    return result;
}

} // namespace felitronics::toml

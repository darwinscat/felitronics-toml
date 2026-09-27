// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml: https://github.com/darwinscat/felitronics-toml

// felitronics::toml: a strict, deterministic subset of TOML 1.0, a parser and a canonical writer.
// Header-only C++20 with no dependencies beyond the standard library. The contract: docs/TOML-SUBSET.md.

#pragma once

#include <algorithm>
#include <array>
#include <cfloat>      // FLT_EVAL_METHOD
#include <cmath>       // signbit preserves negative zero without assuming byte order
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
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
template <class T> void grow (std::vector<T>& v)
{
    // A shared growth rule makes cumulative requests less than four slots per appended element.
    if (v.size() == v.capacity()) v.reserve (v.empty() ? 1 : v.size() * 2);
}
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
namespace detail { template <bool> class Parser; struct Layers; struct StorageCount; }

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

    // How write() spells the table where TOML leaves a choice: under a [header] of its own (the default), or inline
    // as { ... } on its key's line. The parser marks the tables it read in braces; a caller may set either. A table
    // inside an inline table is always inline, whatever its own style. Not part of equality.
    enum class Style : std::uint8_t { Header, Inline };
    Style style = Style::Header;
    // Where the table was defined: its [header], the { of an inline table, or the key that implied it. The root
    // of a parsed document is at 1:1. Not part of equality.
    Position position {};

private:
    struct Node { std::size_t left = 0, right = 0; int height = 1; };
    std::vector<Entry> entries_;
    std::vector<Node> index_;     // one-based links; zero is the empty subtree
    std::size_t root_ = 0;
    enum class Origin { Implicit, Dotted, Header, Inline };   // Inline: a value, closed to headers and dotted keys
    Origin origin_ = Origin::Implicit; // parser bookkeeping, excluded from value equality
    [[nodiscard]] int height (std::size_t n) const noexcept;
    void refresh (std::size_t n) noexcept;
    [[nodiscard]] std::size_t rotate (std::size_t n, bool left) noexcept;
    [[nodiscard]] std::size_t link (std::size_t n, std::size_t added) noexcept;
    [[nodiscard]] std::size_t locate (std::string_view key) const noexcept;
    template <bool> friend class detail::Parser;
    friend struct detail::StorageCount;
    friend struct detail::Layers;
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
    : style (other.style), position (other.position), entries_ (std::move (other.entries_)), index_ (std::move (other.index_)),
      root_ (std::exchange (other.root_, 0)), origin_ (other.origin_) {}
inline Table& Table::operator= (const Table&) = default;
inline Table& Table::operator= (Table&& other) noexcept
{
    if (this != &other)
    {
        style = other.style; position = other.position;
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
    detail::grow (entries_);
    detail::grow (index_);
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
    ExpectedHeaderEnd, TrailingCharacters, DuplicateKey, RedefinedTable, TableValueConflict, EntryLimit,
    UnterminatedInlineTable, ExpectedInlineTableSeparator
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
        FELITRONICS_TOML_CODE(UnterminatedInlineTable) FELITRONICS_TOML_CODE(ExpectedInlineTableSeparator)
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
template <class String> void appendUtf8 (String& s, std::uint32_t u)
{
    if (u < 0x80) s += char (u);
    else if (u < 0x800) { s += char (0xC0u | (u >> 6)); s += char (0x80u | (u & 63u)); }
    else if (u < 0x10000)
    { s += char (0xE0u | (u >> 12)); s += char (0x80u | ((u >> 6) & 63u)); s += char (0x80u | (u & 63u)); }
    else
    { s += char (0xF0u | (u >> 18)); s += char (0x80u | ((u >> 12) & 63u));
      s += char (0x80u | ((u >> 6) & 63u)); s += char (0x80u | (u & 63u)); }
}

[[nodiscard]] constexpr std::size_t storageAdd (std::size_t a, std::size_t b) noexcept
{
    return b > std::numeric_limits<std::size_t>::max() - a ? std::numeric_limits<std::size_t>::max() : a + b;
}
[[nodiscard]] constexpr std::size_t storageMultiply (std::size_t a, std::size_t b) noexcept
{
    return b != 0 && a > std::numeric_limits<std::size_t>::max() / b ? std::numeric_limits<std::size_t>::max() : a * b;
}
// One byte of a basic string's content, escaped where it must be.
template <class String> void escaped (String& out, char c)
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
template <class String> void quoted (String& out, std::string_view s)
{
    out += '"';
    for (const char c : s) escaped (out, c);
    out += '"';
}
template <class String> void keyText (String& out, std::string_view key)
{
    if (! key.empty() && std::all_of (key.begin(), key.end(), bare)) out += key;
    else quoted (out, key);
}

// Size-only strings let validation and counting use the same parser and writer as materialization.
struct CountText
{
    std::size_t n = 0;
    CountText() = default;
    CountText (const char* s) : n (std::char_traits<char>::length (s)) {}
    CountText& operator+= (const char* s) { n += std::char_traits<char>::length (s); return *this; }
    CountText& operator+= (char) { ++n; return *this; }
    CountText& operator+= (std::string_view s) { n += s.size(); return *this; }
    CountText& operator+= (const CountText& s) { n += s.n; return *this; }
    std::size_t size() const { return n; }
    bool empty() const { return n == 0; }
};
struct CountString
{
    // Keys fit in this prefix, so their spelling uses keyText just like the writer and Reader.
    // Numeric conversion needs at most 20 digits; longer value strings retain only their size.
    std::array<char, kMaxKey> first {};
    std::size_t n = 0;
    CountString& operator+= (char c)
    {
        if (n < first.size()) first[n] = c;
        ++n;
        return *this;
    }
    void append (std::string_view s) { for (char c : s) *this += c; }
    std::size_t size() const { return n; }
    bool empty() const { return n == 0; }
    char operator[] (std::size_t i) const { return first[i]; }
    const char* begin() const { return first.data(); }
    const char* end() const { return first.data() + std::min (n, first.size()); }
    // Called only for successfully parsed keys (n <= kMaxKey).
    std::size_t spelled() const
    {
        CountText out;
        keyText (out, std::string_view (first.data(), n));
        return out.size();
    }
};
struct CountTable
{
    enum class Origin { Inline };
    using Style = toml::Table::Style;
    Origin origin_ = Origin::Inline;
    Style style = Style::Inline;
    Position position {};
};
template <class T> struct CountVector
{
    T first {};
    std::size_t n = 0;
    void push_back (T v) { if (n++ == 0) first = std::move (v); }
    std::size_t size() const { return n; }
    bool empty() const { return n == 0; }
    const T& front() const { return first; }
};
struct CountValue
{
    struct Data { std::size_t type = 0; std::size_t index() const { return type; } } data;
    Position position {};
    CountValue() = default;
    CountValue (CountString) {}
    CountValue (std::int64_t) : data { 1 } {}
    CountValue (Decimal) : data { 2 } {}
    CountValue (bool) : data { 3 } {}
    CountValue (CountVector<CountValue>) : data { 4 } {}
    CountValue (CountTable) : data { 5 } {}
    CountValue (CountVector<CountTable>) : data { 6 } {}
};
struct StorageCount
{
    std::size_t parse = 0, readValues = 0, problems = 0, paths = 0, rows = 0, values = 0, tables = 0;
    // Two pointers per checked-iterator proxy in the Microsoft standard library, including empty containers.
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL != 0
    static constexpr std::size_t proxyBytes = 2 * sizeof (void*);
#else
    static constexpr std::size_t proxyBytes = 0;
#endif
    // MSVC's allocator aligns requests of at least 4096 bytes to 32 bytes and stores the
    // original pointer (plus a Debug sentinel). These bytes reach operator new too.
#if defined(_MSC_VER)
 #if defined(_DEBUG)
    static constexpr std::size_t alignmentBytes = 31 + 2 * sizeof (void*);
 #else
    static constexpr std::size_t alignmentBytes = 31 + sizeof (void*);
 #endif
#else
    static constexpr std::size_t alignmentBytes = 0;
#endif
    static std::size_t stringAlignment (std::size_t n)
    {
        if constexpr (alignmentBytes == 0) return 0;
        std::size_t requests = 0;
        // A large request cannot precede 2048 characters with growth at most twofold.
        // Subsequent capacities grow by at least floor(1.5 * capacity). Starting below
        // MSVC's first large capacity also covers rounding, copies and bulk appends.
        for (std::size_t size = 2048; size <= n;)
        {
            ++requests;
            const auto next = storageAdd (size, size / 2);
            if (next == size) break;
            size = next;
        }
        return storageMultiply (requests, alignmentBytes);
    }
    static std::size_t vectorAlignment (std::size_t element, std::size_t n)
    {
        if constexpr (alignmentBytes == 0) return 0;
        std::size_t capacity = 1;
        while (storageMultiply (capacity, element) < 4096) capacity *= 2;
        // Each large doubling request consumes at least capacity/2 new elements.
        // This bound also holds when n elements are spread over many separate vectors;
        // reserve-once conversion and Reader bit vectors need no more requests.
        return storageMultiply (n / std::max (capacity / 2, std::size_t (1)), alignmentBytes);
    }
    std::size_t parseBytes() const
    {
        auto bytes = storageAdd (parse, vectorAlignment (sizeof (Entry), problems));
        bytes = storageAdd (bytes, vectorAlignment (sizeof (Table::Node), problems));
        bytes = storageAdd (bytes, vectorAlignment (sizeof (Value), values));
        bytes = storageAdd (bytes, vectorAlignment (sizeof (Table), tables));
        return storageMultiply (2, bytes);
    }
    // The smallest short-string capacity of the supported standard libraries, for each pointer width.
    static constexpr std::size_t smallString = sizeof (void*) == 4 ? 10 : 15;
    static std::size_t stringBytes (std::size_t n)
    {
        if (n <= smallString) return 0;
        const auto bytes = storageAdd (n, 1);
        // MSVC's 1.5-fold string growth approaches 4.5 requested bytes per character. K = 2
        // times 2.25 character slots covers it, including the terminator and allocation rounding.
        return storageAdd (storageAdd (storageMultiply (2, bytes), bytes / 4 + (bytes % 4 != 0 ? 1u : 0u)),
                           stringAlignment (n));
    }
    void proxy (std::size_t n) { parse = storageAdd (parse, n * proxyBytes); }
    void string (std::size_t n, bool value)
    {
        parse = storageAdd (parse, stringBytes (n));
        if (value) readValues = storageAdd (readValues, stringBytes (n));
    }
    void entry (const CountString& key, std::size_t prefix)
    {
        parse = storageAdd (parse, 2 * (sizeof (Entry) + sizeof (Table::Node)) + stringBytes (key.size()));
        proxy (12);
        ++problems;
        paths = storageAdd (paths, stringBytes (prefix + key.spelled()));
    }
};

template <bool Counting = false> class Parser
{
    template <bool> friend class Parser;
    using String = std::conditional_t<Counting, CountString, std::string>;
    using Value = std::conditional_t<Counting, CountValue, toml::Value>;
    using Table = std::conditional_t<Counting, CountTable, toml::Table>;
    using Array = std::conditional_t<Counting, CountVector<Value>, toml::Array>;
    using Tables = std::conditional_t<Counting, CountVector<Table>, toml::Tables>;
    struct Key { String name; std::size_t pos; Position position; };
    using Keys = std::conditional_t<Counting, CountVector<Key>, std::vector<Key>>;
public:
    StorageCount storage {};
    Parser (std::string_view s, std::uint32_t source) : text_ (s), source_ (source) {}
    [[nodiscard]] std::variant<Table, Error> run()
    {
        if (text_.size() > kMaxDocument) return errorAt (Code::DocumentLimit, kMaxDocument);
        scanEncoding();
        end_ = lexical_ ? lexical_->second : text_.size();
        Table root;
        if constexpr (Counting) storage.proxy (4);
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
                if (array) { ++pos_; arrayPaths_ = true; }
                pathBytes_ = 0;
                auto path = keys (0, false, true);
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
                const auto prefix = pathBytes_;
                auto path = keys (currentDepth);
                if (failed_) break;
                if (peek() != '=') { fail (Code::ExpectedEquals, pos_); break; }
                ++pos_; spaces();
                Value value = readValue (currentDepth + path.size());
                if (failed_ || ! lineEnd()) break;
                assign (*current, path, std::move (value));
                pathBytes_ = prefix;
            }
        }
        if (lexical_ && (! failed_ || lexical_->second <= failurePos_))
            return errorAt (lexical_->first, lexical_->second);
        if (failed_) return errorAt (failureCode_, failurePos_);
        return root;
    }

private:
    struct StringCharge
    {
        Parser& parser;
        const String& string;
        bool enabled = true, value = false;
        ~StringCharge() { if constexpr (Counting) if (enabled) parser.storage.string (string.size(), value); }
    };
    std::size_t pathBytes_ = 0;
    bool arrayPaths_ = false;
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
    [[nodiscard]] bool lineEndAt (std::size_t p) const noexcept
    {
        return p < end_ && (text_[p] == '\n' || (text_[p] == '\r' && p + 1 < end_ && text_[p + 1] == '\n'));
    }
    // A basic string, or with three quotes a multi-line basic string (never a key): a line ending right after the
    // opening quotes is dropped, CRLF inside reads as LF, a backslash that ends a line drops every space, tab and
    // line ending after it, and one or two quotes may stand right before the closing three.
    [[nodiscard]] String string (bool key)
    {
        String out;
        StringCharge charge { *this, out, ! key, ! key };
        const std::size_t limit = key ? kMaxKey : kMaxString;
        const bool multiline = ! key && text_.substr (pos_, 3) == "\"\"\"" && pos_ + 3 <= end_;
        pos_ += multiline ? 3 : 1;
        if (multiline && lineEndAt (pos_)) pos_ += text_[pos_] == '\r' ? 2u : 1u;
        while (! atEnd() && ! failed_)
        {
            const auto start = pos_;
            char c = text_[pos_++];
            if (c == '"')
            {
                if (! multiline) return out;
                std::size_t run = 1;
                while (run < 5 && start + run < end_ && text_[start + run] == '"') ++run;
                if (run >= 3)
                {
                    for (std::size_t q = 0; q + 3 < run && ! failed_; ++q)
                    {
                        out += '"';
                        if (out.size() > limit) fail (Code::StringLimit, start + q);
                    }
                    pos_ = start + run;
                    if (! failed_) return out;
                    break;
                }
                out += '"';
            }
            else if (c == '\n' || c == '\r')
            {
                if (! multiline) { fail (Code::UnterminatedString, start); break; }
                if (c == '\r') ++pos_;       // scanEncoding guarantees the LF
                out += '\n';
            }
            else if (c == '\\')
            {
                if (atEnd()) { fail (Code::UnterminatedString, pos_); break; }
                if (multiline && (peek() == ' ' || peek() == '\t' || peek() == '\n' || peek() == '\r'))
                {
                    auto p = pos_;
                    while (p < end_ && (text_[p] == ' ' || text_[p] == '\t')) ++p;
                    if (! lineEndAt (p)) { fail (Code::InvalidEscape, pos_); break; }
                    while (p < end_ && (text_[p] == ' ' || text_[p] == '\t' || lineEndAt (p))) p += text_[p] == '\r' ? 2u : 1u;
                    pos_ = p;
                    continue;
                }
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
            if (out.size() > limit) fail (key ? Code::KeyLimit : Code::StringLimit, start);
        }
        if (! failed_) fail (Code::UnterminatedString, pos_);
        return out;
    }
    // Inside an inline table (braced), EOF, a line ending or a comment where a key belongs is UnterminatedInlineTable.
    [[nodiscard]] bool lineEnds() const noexcept { return atEnd() || peek() == '\n' || peek() == '\r' || peek() == '#'; }
    [[nodiscard]] Keys keys (std::size_t base, bool braced = false, bool heading = false)
    {
        Keys path;
        if constexpr (Counting) storage.proxy (1);
        for (;;)
        {
            spaces();
            if (braced && lineEnds()) { fail (Code::UnterminatedInlineTable, pos_); break; }
            if (base + path.size() == kMaxDepth) { fail (Code::DepthLimit, pos_); break; }
            const auto start = pos_;
            String name;
            StringCharge charge { *this, name };
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
            if constexpr (Counting)
            {
                storage.entry (name, pathBytes_ + (arrayPaths_ ? 7 * (base + path.size() + 1) : 0));
                pathBytes_ += name.spelled() + 1;
                storage.parse = storageAdd (storage.parse, sizeof (typename Parser<false>::Key) * (path.size() == 0 ? 1 : 2));
            }
            if constexpr (! Counting) grow (path);
            path.push_back ({ std::move (name), start, position (start) });
            spaces();
            if constexpr (Counting)
                if (heading || peek() == '.')
                    storage.paths = storageAdd (storage.paths,
                        2 * StorageCount::stringBytes (pathBytes_ + (arrayPaths_ ? 7 * (base + path.size()) : 0)));
            if (peek() != '.') break;
            ++pos_;
        }
        if constexpr (Counting)
            storage.parse = storageAdd (storage.parse, StorageCount::vectorAlignment (sizeof (typename Parser<false>::Key), path.size()));
        return path;
    }
    [[nodiscard]] Value scalar()
    {
        // Checked strings allocate a proxy even for short numeric tokens; string values also move
        // through Value and array growth. Three counted proxies cover six such requests after K.
        if constexpr (Counting) storage.proxy (3);
        const auto start = pos_;
        if (peek() == '"') return string (false);
        if (atEnd() || peek() == '#' || peek() == '\n' || peek() == '\r' || peek() == ',' || peek() == ']' || peek() == '}')
        { fail (Code::ExpectedValue, pos_); return {}; }
        if (! digit (peek()) && peek() != '+' && peek() != '-' && peek() != 't' && peek() != 'f')
        { fail (Code::UnsupportedValue, pos_); return {}; }
        while (! atEnd() && peek() != ' ' && peek() != '\t' && peek() != '\r' && peek() != '\n'
               && peek() != ',' && peek() != ']' && peek() != '}' && peek() != '#') ++pos_;
        if (lexical_ && pos_ == end_)
        { fail (lexical_->first, end_); return {}; }
        const auto token = text_.substr (start, pos_ - start);
        if (token == "true") return true;
        if (token == "false") return false;
        std::size_t p = 0;
        const bool negative = token[p] == '-';
        if (token[p] == '-' || token[p] == '+') ++p;
        // Digits, each underscore between two of them (TOML's rule). The digits are collected without them.
        String digits;
        StringCharge charge { *this, digits };
        const auto run = [&] (bool fraction, std::uint8_t& scale)
        {
            for (const auto first = p; p < token.size() && (digit (token[p]) || token[p] == '_'); ++p)
            {
                if (token[p] == '_')
                {
                    if (p == first || ! digit (token[p - 1]) || p + 1 == token.size() || ! digit (token[p + 1])) return false;
                    continue;
                }
                if (fraction && scale == 9) { fail (Code::DecimalScale, start + p); return false; }
                if (fraction) ++scale;
                digits += token[p];
            }
            return true;
        };
        std::uint8_t scale = 0;
        if (! run (false, scale) || digits.empty() || (digits.size() > 1 && digits[0] == '0'))
        { fail (Code::InvalidNumber, start); return {}; }
        if (p < token.size() && token[p] == '.')
        {
            ++p;
            const bool fine = run (true, scale);
            if (failed_) return {};
            if (! fine || scale == 0) { fail (Code::InvalidNumber, start); return {}; }
        }
        if (p != token.size()) { fail (Code::InvalidNumber, start); return {}; }
        const std::uint64_t limit = scale != 0 ? std::uint64_t (Decimal::kMaxMantissa)
            : std::uint64_t (std::numeric_limits<std::int64_t>::max()) + (negative ? 1u : 0u);
        std::uint64_t n = 0;
        for (const char c : digits)
        {
            const auto d = std::uint64_t (c - '0');
            if (n > (limit - d) / 10)
            { fail (scale != 0 ? Code::DecimalRange : Code::IntegerRange, start); return {}; }
            n = n * 10 + d;
        }
        const auto signedN = negative ? (n == 0 ? std::int64_t (0) : -std::int64_t (n - 1) - 1) : std::int64_t (n);
        if (scale != 0) return Decimal { signedN, scale, negative && n == 0 };
        return signedN;
    }
    // depth: how many key components lead from the root to this value's key. The keys of an inline table, and of
    // each table in an array of them, continue from there.
    [[nodiscard]] Value readValue (std::size_t depth)
    {
        const auto where = position (pos_);
        if (peek() == '{') return inlineTable (depth, where);
        if (peek() != '[')
        {
            Value v = scalar();
            v.position = where;
            return v;
        }
        ++pos_; spaceLines();
        // One type per array: every item the same scalar alternative, or every item an inline table.
        Array items;
        Tables tables;
        if constexpr (Counting) storage.proxy (8);
        if (peek() != ']')
            for (;;)
            {
                if (atEnd()) { fail (Code::UnterminatedArray, pos_); break; }
                if (items.size() + tables.size() == kMaxArray) { fail (Code::ArrayLimit, pos_); break; }
                const auto start = pos_;
                const auto itemWhere = position (start);
                if (peek() == '{')
                {
                    arrayPaths_ = true;
                    Table t = inlineTable (depth, itemWhere);
                    if (failed_) break;
                    if (! items.empty()) { fail (Code::MixedArray, start); break; }
                    if (! count (start)) break;      // each element counts once, as each [[header]] does
                    if constexpr (Counting)
                    {
                        ++storage.tables;
                        storage.parse = storageAdd (storage.parse, 2 * sizeof (toml::Table));
                        storage.proxy (6);
                    }
                    if constexpr (! Counting) grow (tables);
                    tables.push_back (std::move (t));
                }
                else
                {
                    Value v = scalar();
                    if (failed_) break;
                    v.position = itemWhere;
                    if (! tables.empty() || (! items.empty() && v.data.index() != items.front().data.index()))
                    { fail (Code::MixedArray, start); break; }
                    if constexpr (Counting)
                    {
                        ++storage.values;
                        storage.parse = storageAdd (storage.parse, 2 * sizeof (toml::Value));
                        storage.readValues = storageAdd (storage.readValues, sizeof (std::string));
                    }
                    if constexpr (! Counting) grow (items);
                    items.push_back (std::move (v));
                }
                spaceLines();
                if (peek() == ']') break;
                if (atEnd()) { fail (Code::UnterminatedArray, pos_); break; }
                if (peek() != ',') { fail (Code::ExpectedArraySeparator, pos_); break; }
                ++pos_; spaceLines();
                if (peek() == ']') break;
            }
        if (! failed_) ++pos_;
        Value result = tables.empty() ? Value (std::move (items)) : Value (std::move (tables));
        result.position = where;
        return result;
    }
    // { key = value, ... } on one line: no line ending or comment between the braces except inside a value (a
    // multi-line string, an array), no trailing comma. It is a value: closed to later headers and dotted keys.
    [[nodiscard]] Table inlineTable (std::size_t depth, Position where)
    {
        Table t;
        if constexpr (Counting)
        {
            ++storage.rows;
            storage.proxy (12);
            storage.paths = storageAdd (storage.paths, 2 * StorageCount::stringBytes (pathBytes_ + (arrayPaths_ ? 7 * depth : 0)));
        }
        t.origin_ = Table::Origin::Inline;
        t.style = Table::Style::Inline;
        t.position = where;
        ++pos_; spaces();
        if (peek() == '}') { ++pos_; return t; }
        // Between the braces, EOF, a line ending or a comment anywhere outside a value (where a key, =, a value, a
        // comma or the closing brace belongs) is UnterminatedInlineTable.
        for (;;)
        {
            const auto prefix = pathBytes_;
            auto path = keys (depth, true);
            if (failed_) break;
            if (lineEnds()) { fail (Code::UnterminatedInlineTable, pos_); break; }
            if (peek() != '=') { fail (Code::ExpectedEquals, pos_); break; }
            ++pos_; spaces();
            if (lineEnds()) { fail (Code::UnterminatedInlineTable, pos_); break; }
            Value value = readValue (depth + path.size());
            if (failed_) break;
            assign (t, path, std::move (value));
            pathBytes_ = prefix;
            if (failed_) break;
            spaces();
            if (peek() == '}') { ++pos_; break; }
            if (peek() != ',')
            {
                fail (lineEnds() ? Code::UnterminatedInlineTable : Code::ExpectedInlineTableSeparator, pos_);
                break;
            }
            ++pos_; spaces();
        }
        return t;
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
            if (child->origin_ == Table::Origin::Inline) { fail (Code::TableValueConflict, k.pos); return nullptr; }
            if (dotted && child->origin_ == Table::Origin::Header)
            { fail (Code::RedefinedTable, k.pos); return nullptr; }
            if (dotted) child->origin_ = Table::Origin::Dotted;
            return child;
        }
        if (! dotted)
            if (auto* a = std::get_if<Tables> (&v->data); a != nullptr && ! a->empty() && a->back().origin_ != Table::Origin::Inline)
                return &a->back();
        fail (Code::TableValueConflict, k.pos); return nullptr;
    }
    void assign (Table& t, const Keys& path, Value v)
    {
        if constexpr (Counting) { (void) t; (void) path; (void) v; }
        else
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
                // A key assigned before is a duplicate, inline tables included. A table that headers or dotted keys
                // built cannot become a value: that is a conflict of roles.
                const auto* table = std::get_if<Table> (&old->data);
                const auto* tables = std::get_if<Tables> (&old->data);
                const bool built = (table != nullptr && table->origin_ != Table::Origin::Inline)
                                || (tables != nullptr && ! tables->empty() && tables->back().origin_ != Table::Origin::Inline);
                fail (built ? Code::TableValueConflict : Code::DuplicateKey, key.pos); return;
            }
            (void) add (*parent, key, std::move (v));
        }
    }

    [[nodiscard]] Table* header (Table& root, const Keys& path, bool array, Position where)
    {
        if constexpr (Counting)
        {
            (void) path; (void) where;
            if (array)
            {
                ++storage.rows;
                ++storage.tables;
                storage.parse = storageAdd (storage.parse, 2 * sizeof (toml::Table));
                storage.proxy (10);
            }
            return &root;
        }
        else
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
                if (auto* a = std::get_if<Tables> (&v->data); a != nullptr && (a->empty() || a->back().origin_ != Table::Origin::Inline))
                {
                    if (a->size() == kMaxArray) { fail (Code::ArrayLimit, key.pos); return nullptr; }
                    if (! count (key.pos)) return nullptr;
                    grow (*a);
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
                    if (child->origin_ == Table::Origin::Inline) { fail (Code::TableValueConflict, key.pos); return nullptr; }
                    if (child->origin_ != Table::Origin::Implicit) { fail (Code::RedefinedTable, key.pos); return nullptr; }
                    child->origin_ = Table::Origin::Header;
                    child->position = v->position = where;
                    return child;
                }
            }
            fail (Code::TableValueConflict, key.pos); return nullptr;
        }
    }

};
} // namespace detail


namespace detail
{
template <class String> void unsignedText (String& out, std::uint64_t n)
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
// The multi-line spelling: the opening quotes on the key's line, the text from the next line on, a raw LF for
// each line feed. A quote stays raw unless another quote or the closing delimiter follows it, so no run of
// quotes can close the string early. Everything else is escaped as in a one-line string.
template <class String> void multilineQuoted (String& out, std::string_view s)
{
    out += "\"\"\"\n";
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '\n' || (s[i] == '"' && i + 1 < s.size() && s[i + 1] != '"')) out += s[i];
        else escaped (out, s[i]);
    }
    out += "\"\"\"";
}

template <class String = std::string> class Writer
{
public:
    [[nodiscard]] std::optional<String> run (const Table& root)
    {
        table (root, "", 0);
        if (failed_) return std::nullopt;
        return std::move (out_);
    }
private:
    String out_;
    std::size_t entries_ = 0;
    bool failed_ = false;
    void bound() { if (out_.size() > kMaxDocument) failed_ = true; }
    void count() { if (++entries_ > kMaxEntries) failed_ = true; }
    void scalar (const Value& value, bool statement)
    {
        if (const auto* s = std::get_if<std::string> (&value.data))
        {
            if (! validString (*s, kMaxString)) { failed_ = true; return; }
            if (statement && s->find ('\n') != std::string::npos) multilineQuoted (out_, *s);
            else quoted (out_, *s);
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
    [[nodiscard]] static bool inlineElements (const Tables& a) noexcept
    {
        return ! a.empty() && std::all_of (a.begin(), a.end(), [] (const Table& t) { return t.style == Table::Style::Inline; });
    }
    // Spelled on its key's line: a scalar, a scalar array, an inline table, or an array whose tables are all inline.
    [[nodiscard]] static bool statement (const Value& v) noexcept
    {
        if (const auto* t = std::get_if<Table> (&v.data)) return t->style == Table::Style::Inline;
        if (const auto* a = std::get_if<Tables> (&v.data)) return inlineElements (*a);
        return true;
    }
    // depth: how many key components lead from the root to the value's key. statement: the value is the whole
    // right-hand side of a key = value line, where a string with a line feed is spelled on several lines and an
    // array of inline tables puts one table on each line. Inside an array or an inline table, everything stays on
    // one line.
    void value (const Value& v, std::size_t depth, bool statement)
    {
        if (const auto* a = std::get_if<Array> (&v.data))
        {
            if (a->size() > kMaxArray) { failed_ = true; return; }
            out_ += '[';
            for (std::size_t i = 0; i < a->size() && ! failed_; ++i)
            {
                if ((*a)[i].data.index() != a->front().data.index()) { failed_ = true; return; }
                if (i != 0) out_ += ", ";
                scalar ((*a)[i], false);
            }
            out_ += ']';
        }
        else if (const auto* t = std::get_if<Table> (&v.data)) inlineTable (*t, depth);
        else if (const auto* ts = std::get_if<Tables> (&v.data))
        {
            if (ts->empty() || ts->size() > kMaxArray) { failed_ = true; return; }
            out_ += statement ? "[\n" : "[";
            for (std::size_t i = 0; i < ts->size() && ! failed_; ++i)
            {
                count();
                if (statement) out_ += "    ";
                else if (i != 0) out_ += ", ";
                inlineTable ((*ts)[i], depth);
                if (statement) out_ += ",\n";
                bound();
            }
            out_ += ']';
        }
        else scalar (v, statement);
    }
    // { key = value, ... } in entry order, whatever the style of the tables inside it. depth: the table's own.
    void inlineTable (const Table& t, std::size_t depth)
    {
        if (depth > kMaxDepth) { failed_ = true; return; }
        if (t.entries().empty()) { out_ += "{}"; return; }
        out_ += "{ ";
        for (std::size_t i = 0; i < t.entries().size() && ! failed_; ++i)
        {
            const auto& e = t.entries()[i];
            count();
            if (depth == kMaxDepth || ! validString (e.key, kMaxKey)) { failed_ = true; return; }
            if (i != 0) out_ += ", ";
            keyText (out_, e.key); out_ += " = "; value (e.value, depth + 1, false);
            bound();
        }
        out_ += " }";
    }
    void heading (const String& path, bool array)
    {
        if (! out_.empty()) out_ += '\n';
        out_ += array ? "[[" : "[";
        out_ += path;
        out_ += array ? "]]\n" : "]\n";
        bound();
    }
    void table (const Table& t, const String& path, std::size_t depth)
    {
        // Checking before descent caps call-stack depth even for an invalid caller-built tree.
        if (depth > kMaxDepth) { failed_ = true; return; }
        for (const auto& e : t.entries())
        {
            count();
            if (depth == kMaxDepth || ! validString (e.key, kMaxKey)) failed_ = true;
            if (failed_) return;
            if (statement (e.value))
            {
                keyText (out_, e.key); out_ += " = "; value (e.value, depth + 1, true); out_ += '\n'; bound();
                if (failed_) return;
            }
        }
        // Grouping is required by TOML's current-table rule. Order within each group is caller order.
        for (int group = 0; group < 2; ++group)
            for (const auto& e : t.entries())
            {
                if (failed_) return;
                if (statement (e.value)) continue;
                const auto* child = std::get_if<Table> (&e.value.data);
                const auto* array = std::get_if<Tables> (&e.value.data);
                if ((group == 0 && child == nullptr) || (group == 1 && array == nullptr)) continue;
                String next = path;
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

namespace detail
{
// Walks the two trees with a stack of its own. A table's entries are all replaced or appended before any of its
// children is visited, so the child pointers taken afterwards stay valid: nothing inserts into that vector again.
struct Layers
{
    static void merge (Table& into, const Table& top)
    {
        std::vector<std::pair<Table*, const Table*>> work { { &into, &top } };
        std::vector<std::pair<std::size_t, const Table*>> nested;
        while (! work.empty())
        {
            auto [dst, src] = work.back();
            work.pop_back();
            nested.clear();
            for (const auto& e : src->entries())
            {
                const auto n = dst->locate (e.key);
                if (n == 0) { (void) dst->insert (e.key, e.value, e.keyPosition); continue; }
                auto& old = dst->entries_[n - 1];
                const auto* child = std::get_if<Table> (&e.value.data);
                if (child != nullptr && std::holds_alternative<Table> (old.value.data)) nested.emplace_back (n - 1, child);
                else { old.value = e.value; old.keyPosition = e.keyPosition; }
            }
            for (const auto& [i, child] : nested) work.emplace_back (std::get_if<Table> (&dst->entries_[i].value.data), child);
        }
    }
};
}

// The tree base becomes with top laid over it. Where both hold a table at a key, the two merge key by key, at every
// depth. Anywhere else top's value replaces base's whole: a scalar, an array, an array of tables, and a table that
// meets anything but a table. Keys only in base stay; keys only in top are appended, in top's order. A merged table
// keeps base's place, style and position; a replaced entry takes top's key position with top's value. Every value
// keeps its own position, so position.source tells which layer it came from when each document was parsed with its
// own source number. Layers apply in order: overlay(overlay(defaults, user), project). It never fails; the result
// may exceed the document limits, which writeChecked() then refuses.
[[nodiscard]] inline Table overlay (const Table& base, const Table& top)
{
    Table out = base;
    detail::Layers::merge (out, top);
    return out;
}

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
// Cumulative bytes requested through operator new by one parse, including temporary storage.
// Allocation-free; semantic conflicts may be counted through the remaining syntactically valid input.
[[nodiscard]] inline std::size_t storageFor (std::string_view text) noexcept
{
    detail::Parser<true> counter (text, 0);
    (void) counter.run();
    return counter.storage.parseBytes();
}
// source is copied into every Position of the tree, so values from different documents stay apart after overlay().
[[nodiscard]] inline ParseResult parse (std::string_view text, std::uint32_t source = 0) noexcept
{
    auto result = detail::Parser<> (text, source).run();
    if (const auto* root = std::get_if<Table> (&result); root != nullptr && ! detail::Writer<detail::CountText>{}.run (*root))
    {
        // Canonical spacing/escapes can grow a compact input. Refuse it here so EVERY successful parse can
        // be written and reparsed under the same 1 MiB limit. This resource check follows syntax validation.
        return detail::errorAt (Code::CanonicalLimit, text, text.size());
    }
    return result;
}

} // namespace felitronics::toml

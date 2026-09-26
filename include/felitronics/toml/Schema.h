// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml: https://github.com/darwinscat/felitronics-toml

// felitronics::toml typed reading: fields with a type, a range, a default or a requirement, read from a parsed Table
// into the caller's own structs, and every key the caller did not read reported. No exceptions: each problem is data,
// a stable code with the key path and the position to point at. The contract: docs/TOML-SUBSET.md, "Typed reading".

#pragma once

#include <felitronics/toml/Toml.h>

#include <concepts>
#include <type_traits>

namespace felitronics::toml
{

enum class Fault
{
    Missing,        // a required key is absent; points at the table that lacks it
    WrongType,      // the value has another type; points at the value
    OutOfRange,     // outside the field's range, or not representable in the field's type; points at the value
    UnknownKey,     // a key the schema did not read (a typo, most often); points at the key
    Refused         // the caller's own check refused the value (Reader::refuse); points at the value
};
[[nodiscard]] inline const char* faultName (Fault fault) noexcept
{
    switch (fault)
    {
        case Fault::Missing: return "Missing";
        case Fault::WrongType: return "WrongType";
        case Fault::OutOfRange: return "OutOfRange";
        case Fault::UnknownKey: return "UnknownKey";
        case Fault::Refused: return "Refused";
    }
    return "Unknown";
}
enum class Severity { Error, Warning };
enum class Need { Required, Optional };

struct Problem
{
    Fault fault;
    Severity severity;
    std::string path;            // the key path in TOML spelling, array elements by index: limiter.release, bands[2].gain
    Position position;           // see Fault; a table's position for Missing (the root of a document is at 1:1)
    std::uint32_t detail = 0;    // the caller's own code, for Refused
};
struct Report
{
    std::vector<Problem> problems;       // in the order they were found
    [[nodiscard]] bool ok() const noexcept
    {
        return std::none_of (problems.begin(), problems.end(), [] (const Problem& p) { return p.severity == Severity::Error; });
    }
};
struct ReadOptions
{
    Severity unknownKeys = Severity::Error;   // Warning: report keys the schema did not read, and still read the rest
};

// Inclusive bounds; either may be absent: {min, max}, {min}, {std::nullopt, max}. Decimals compare exactly, whatever
// their scales. A constructor, not an aggregate, so {min} alone raises no missing-initializer warning.
template <class T> struct Range
{
    std::optional<T> min, max;
    Range() = default;
    Range (std::optional<T> lowest, std::optional<T> highest = std::nullopt) : min (std::move (lowest)), max (std::move (highest)) {}
};

// A decimal from a Decimal, or losslessly from an integer: n reads as n.0, mantissa n * 10 at scale 1, the value a
// document spelling n.0 would give. That holds while |n| <= 900719925474099 (2^53 / 10); beyond it, nullopt.
[[nodiscard]] inline std::optional<Decimal> asDecimal (const Value& value) noexcept
{
    if (const auto* d = std::get_if<Decimal> (&value.data)) return *d;
    if (const auto* n = std::get_if<std::int64_t> (&value.data))
    {
        constexpr std::int64_t limit = Decimal::kMaxMantissa / 10;
        if (*n >= -limit && *n <= limit) return Decimal { *n * 10, 1, false };
    }
    return std::nullopt;
}

namespace detail
{
struct Unbounded {};   // strings and booleans have no range
template <class T> struct Bounds { using type = Range<T>; };
template <> struct Bounds<std::string> { using type = Unbounded; };
template <> struct Bounds<bool> { using type = Unbounded; };
template <class T> struct Bounds<std::vector<T>> { using type = typename Bounds<T>::type; };
template <class T> struct IsVector : std::false_type {};
template <class T> struct IsVector<std::vector<T>> : std::true_type {};
// The standard integer types, the ones std::in_range takes: not bool, not the character types.
template <class T> concept Integer = std::integral<T> && ! std::same_as<T, bool> && ! std::same_as<T, char>
    && ! std::same_as<T, wchar_t> && ! std::same_as<T, char8_t> && ! std::same_as<T, char16_t> && ! std::same_as<T, char32_t>;

// -1, 0 or 1 for x < y, x == y, x > y, exactly: a negative zero is zero, and 1.5 equals 1.50.
[[nodiscard]] inline int compare (const Decimal& x, const Decimal& y) noexcept
{
    const bool xn = x.mantissa < 0, yn = y.mantissa < 0;
    if (xn != yn) return xn ? -1 : 1;
    auto a = magnitude (x.mantissa), b = magnitude (y.mantissa);
    int flip = xn ? -1 : 1;
    auto sa = x.scale, sb = y.scale;
    if (sa > sb) { std::swap (a, b); std::swap (sa, sb); flip = -flip; }
    // a / 10^sa against b / 10^sb, sa <= sb: a * 10^k against b, without the product.
    const auto power = power10 (std::uint8_t (sb - sa));
    const auto q = b / power, r = b % power;
    const int c = a != q ? (a < q ? -1 : 1) : (r == 0 ? 0 : -1);
    return c * flip;
}
template <class T> [[nodiscard]] bool outside (const T& x, const Range<T>& r)
{
    return (r.min && x < *r.min) || (r.max && *r.max < x);
}
[[nodiscard]] inline bool outside (const Decimal& x, const Range<Decimal>& r)
{
    return (r.min && (! r.min->valid() || compare (x, *r.min) < 0)) || (r.max && (! r.max->valid() || compare (x, *r.max) > 0));
}
inline std::optional<Fault> convert (const Value& v, std::string& out, Unbounded)
{
    const auto* s = std::get_if<std::string> (&v.data);
    if (s == nullptr) return Fault::WrongType;
    out = *s;
    return std::nullopt;
}
inline std::optional<Fault> convert (const Value& v, bool& out, Unbounded)
{
    const auto* b = std::get_if<bool> (&v.data);
    if (b == nullptr) return Fault::WrongType;
    out = *b;
    return std::nullopt;
}
inline std::optional<Fault> convert (const Value& v, Decimal& out, const Range<Decimal>& r)
{
    if (! std::holds_alternative<Decimal> (v.data) && ! std::holds_alternative<std::int64_t> (v.data)) return Fault::WrongType;
    const auto d = asDecimal (v);
    if (! d || outside (*d, r)) return Fault::OutOfRange;
    out = *d;
    return std::nullopt;
}
// The decimal's correctly rounded double, compared with the range as doubles.
inline std::optional<Fault> convert (const Value& v, double& out, const Range<double>& r)
{
    Decimal d;
    if (const auto fault = convert (v, d, Range<Decimal>{})) return fault;
    const double x = d.toDouble();
    if (outside (x, r)) return Fault::OutOfRange;
    out = x;
    return std::nullopt;
}
template <Integer T> std::optional<Fault> convert (const Value& v, T& out, const Range<T>& r)
{
    const auto* n = std::get_if<std::int64_t> (&v.data);
    if (n == nullptr) return Fault::WrongType;
    if (! std::in_range<T> (*n)) return Fault::OutOfRange;
    const auto x = static_cast<T> (*n);
    if (outside (x, r)) return Fault::OutOfRange;
    out = x;
    return std::nullopt;
}
}

// Reads one table. required() and optional() store a field into the caller's variable and return true, or leave it
// unchanged and return false: when the key is absent (a Missing problem if it was required; an optional field keeps
// its default, the value the variable already had), or when the value has another type or is out of range (a
// problem either way). Supported types: bool, std::string, Decimal (an integer reads as n.0), double (a decimal's
// correctly rounded value), any integer type (the value must fit it), and std::vector of any of them for a scalar
// array (the range applies to each item; the first bad item is the one reported). table() and tables() hand a Reader
// for a sub-table, or for each table of an array of tables, to a callback, then report what it left unread. Every
// key read in any way counts as known; finish() reports the rest as UnknownKey.
class Reader
{
public:
    Reader (const Table& table, Report& report, ReadOptions options = {})
        : Reader (table, report, options, std::string{}) {}
    Reader (const Reader&) = delete;
    Reader& operator= (const Reader&) = delete;

    template <class T> bool required (std::string_view key, T& out, typename detail::Bounds<T>::type range = {})
    {
        return get (key, out, range, Need::Required);
    }
    template <class T> bool optional (std::string_view key, T& out, typename detail::Bounds<T>::type range = {})
    {
        return get (key, out, range, Need::Optional);
    }
    // fn (Reader&) for the table at key. False when it is absent or not a table.
    template <class F> bool table (std::string_view key, Need need, F&& fn)
    {
        const Value* v = field (key, need);
        if (v == nullptr) return false;
        const auto* t = std::get_if<Table> (&v->data);
        if (t == nullptr) { problem (Fault::WrongType, pathOf (key), v->position); return false; }
        Reader sub (*t, report_, options_, pathOf (key));
        fn (sub);
        sub.finish();
        return true;
    }
    // fn (Reader&) for each table of the array of tables at key, in order. [] is an array of no tables. False when
    // the key is absent or holds something else.
    template <class F> bool tables (std::string_view key, Need need, F&& fn)
    {
        const Value* v = field (key, need);
        if (v == nullptr) return false;
        if (const auto* a = std::get_if<Array> (&v->data); a != nullptr && a->empty()) return true;
        const auto* ts = std::get_if<Tables> (&v->data);
        if (ts == nullptr) { problem (Fault::WrongType, pathOf (key), v->position); return false; }
        for (std::size_t i = 0; i < ts->size(); ++i)
        {
            std::string element = pathOf (key) + "[";
            detail::unsignedText (element, i);
            Reader row ((*ts)[i], report_, options_, element + "]");
            fn (row);
            row.finish();
        }
        return true;
    }
    // The raw value at key, marked as read, for a type of the caller's own; null when absent (a Missing problem if
    // required).
    [[nodiscard]] const Value* field (std::string_view key, Need need)
    {
        const Entry* e = table_.entry (key);
        if (e == nullptr)
        {
            if (need == Need::Required) problem (Fault::Missing, pathOf (key), table_.position);
            return nullptr;
        }
        used_[std::size_t (e - table_.entries().data())] = true;
        return &e->value;
    }
    // The caller's own check refused the value at key: a Refused problem at the value (at the table when the key is
    // absent), with the caller's code in Problem::detail.
    void refuse (std::string_view key, std::uint32_t detail = 0)
    {
        const Value* v = table_.find (key);
        problem (Fault::Refused, pathOf (key), v ? v->position : table_.position, detail);
    }
    // UnknownKey, with options.unknownKeys as its severity, for every key nothing read, in entry order. Once.
    void finish()
    {
        if (finished_) return;
        finished_ = true;
        for (std::size_t i = 0; i < used_.size(); ++i)
            if (! used_[i])
            {
                const auto& e = table_.entries()[i];
                report_.problems.push_back ({ Fault::UnknownKey, options_.unknownKeys, pathOf (e.key), e.keyPosition, 0 });
            }
    }
    [[nodiscard]] const Table& data() const noexcept { return table_; }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }   // empty for the root

private:
    Reader (const Table& table, Report& report, ReadOptions options, std::string path)
        : table_ (table), report_ (report), options_ (options), path_ (std::move (path)), used_ (table.entries().size(), false) {}
    const Table& table_;
    Report& report_;
    ReadOptions options_;
    std::string path_;
    std::vector<bool> used_;
    bool finished_ = false;

    [[nodiscard]] std::string pathOf (std::string_view key) const
    {
        std::string out = path_;
        if (! out.empty()) out += '.';
        detail::keyText (out, key);
        return out;
    }
    void problem (Fault fault, std::string path, Position position, std::uint32_t detail = 0)
    {
        report_.problems.push_back ({ fault, Severity::Error, std::move (path), position, detail });
    }
    template <class T> bool get (std::string_view key, T& out, const typename detail::Bounds<T>::type& range, Need need)
    {
        const Value* v = field (key, need);
        if (v == nullptr) return false;
        if constexpr (detail::IsVector<T>::value)
        {
            const auto* a = std::get_if<Array> (&v->data);
            if (a == nullptr) { problem (Fault::WrongType, pathOf (key), v->position); return false; }
            T items;
            items.reserve (a->size());
            for (std::size_t i = 0; i < a->size(); ++i)
            {
                typename T::value_type x {};
                if (const auto fault = detail::convert ((*a)[i], x, range))
                {
                    std::string item = pathOf (key) + "[";
                    detail::unsignedText (item, i);
                    problem (*fault, item + "]", (*a)[i].position);
                    return false;
                }
                items.push_back (std::move (x));
            }
            out = std::move (items);
            return true;
        }
        else
        {
            T x {};
            if (const auto fault = detail::convert (*v, x, range)) { problem (*fault, pathOf (key), v->position); return false; }
            out = std::move (x);
            return true;
        }
    }
};

// Reads a whole document: fn (Reader&) for the root, then UnknownKey for whatever the root did not read.
template <class F> [[nodiscard]] Report read (const Table& root, F&& fn, ReadOptions options = {})
{
    Report report;
    Reader reader (root, report, options);
    fn (reader);
    reader.finish();
    return report;
}

} // namespace felitronics::toml

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml: https://github.com/darwinscat/felitronics-toml

// felitronics::toml::embedded: a TOML document compiled into the program as constexpr data by felitronics_toml2cpp
// (tools/toml2cpp.cpp, or the CMake function felitronics_toml_embed). Nothing is parsed at run time: the document is
// an array of nodes, and View walks it, in constant expressions too. toTable() copies it into a Table, positions and
// styles included, for the Reader of Schema.h or for overlay(). The contract: docs/TOML-SUBSET.md, "Embedding".

#pragma once

#include <felitronics/toml/Toml.h>

namespace felitronics::toml::embedded
{

enum class Type : std::uint8_t { String, Integer, Decimal, Boolean, Array, Table, Tables };

// Where a string's bytes are: size bytes at pools[pool] + offset. Plain integers, so a node is a constant without any
// pointer arithmetic to evaluate: a compiler spends no time on it until the string is read.
struct Text { std::uint32_t pool = 0, offset = 0, size = 0; };

// One value. A container's children are nodes[first, first + size): a table's entries in document order, an array's
// items, or the tables of an array of tables.
struct Node
{
    Type type = Type::Table;
    bool inlineStyle = false;        // Table: written { ... } (Table::Style::Inline)
    std::uint8_t scale = 0;          // Decimal
    bool negativeZero = false;       // Decimal
    std::int64_t number = 0;         // Integer; Decimal mantissa; Boolean 0 or 1
    Text key {};                     // a table entry's key; empty for items, the tables of an array and the root
    Text text {};                    // String
    std::uint32_t first = 0, size = 0;
    std::uint32_t line = 0, column = 0, keyLine = 0, keyColumn = 0;
};

class View;
struct Document
{
    const Node* nodes = nullptr;          // nodes[0] is the root table
    const std::uint32_t* order = nullptr; // per table, order[first, first + size) lists its entries' nodes sorted by key
    std::uint32_t count = 0;
    const char* const* pools = nullptr;   // the string bytes
    std::uint32_t poolCount = 0;
    [[nodiscard]] constexpr std::string_view text (Text t) const noexcept
    {
        if (t.size == 0 || t.pool >= poolCount) return {};
        return { pools[t.pool] + t.offset, t.size };
    }
    [[nodiscard]] constexpr View root() const noexcept;
};

// A node of a document, or no node: every accessor of an empty View gives an empty result, so a path can be walked
// without a check at each step: doc.root().find ("limiter").find ("ceiling").decimal(). Presence is a flag of its
// own, never a pointer compared with null: gcc with sanitizers cannot fold &object != nullptr in a constant expression.
class View
{
public:
    constexpr View() noexcept = default;
    constexpr View (const Document& document, std::uint32_t index) noexcept
        : document_ (&document), index_ (index), present_ (index < document.count) {}

    [[nodiscard]] constexpr explicit operator bool() const noexcept { return present_; }
    [[nodiscard]] constexpr std::optional<Type> type() const noexcept
    {
        if (! present_) return std::nullopt;
        return node().type;
    }
    [[nodiscard]] constexpr bool is (Type t) const noexcept { return present_ && node().type == t; }
    [[nodiscard]] constexpr std::string_view key() const noexcept { return present_ ? document_->text (node().key) : std::string_view{}; }
    // Entries of a table, items of an array, tables of an array of tables; 0 for anything else.
    [[nodiscard]] constexpr std::size_t size() const noexcept
    {
        return is (Type::Table) || is (Type::Array) || is (Type::Tables) ? node().size : 0;
    }
    [[nodiscard]] constexpr View operator[] (std::size_t i) const noexcept
    {
        if (i >= size()) return {};
        return View (*document_, node().first + std::uint32_t (i));
    }
    // A table's entry by key, in O(log n) comparisons. Empty when absent, or when this is not a table.
    [[nodiscard]] constexpr View find (std::string_view key) const noexcept
    {
        if (! is (Type::Table)) return {};
        const auto& n = node();
        if (n.first > document_->count || n.size > document_->count - n.first) return {};
        std::uint32_t low = 0, high = n.size;
        while (low < high)
        {
            const auto middle = low + (high - low) / 2;
            const auto candidate = document_->order[n.first + middle];
            if (candidate >= document_->count) return {};
            const int c = key.compare (document_->text (document_->nodes[candidate].key));
            if (c == 0) return View (*document_, candidate);
            if (c < 0) high = middle;
            else low = middle + 1;
        }
        return {};
    }
    [[nodiscard]] constexpr std::optional<std::string_view> string() const noexcept
    {
        if (! is (Type::String)) return std::nullopt;
        return document_->text (node().text);
    }
    [[nodiscard]] constexpr std::optional<std::int64_t> integer() const noexcept
    {
        if (! is (Type::Integer)) return std::nullopt;
        return node().number;
    }
    [[nodiscard]] constexpr std::optional<Decimal> decimal() const noexcept
    {
        if (! is (Type::Decimal)) return std::nullopt;
        return Decimal { node().number, node().scale, node().negativeZero };
    }
    [[nodiscard]] constexpr std::optional<bool> boolean() const noexcept
    {
        if (! is (Type::Boolean)) return std::nullopt;
        return node().number != 0;
    }
    [[nodiscard]] constexpr bool isInline() const noexcept { return is (Type::Table) && node().inlineStyle; }
    // Where the value and its key were written in the embedded document; source 0 (toTable() sets its own).
    [[nodiscard]] constexpr Position position() const noexcept
    {
        return present_ ? Position { node().line, node().column, 0 } : Position{};
    }
    [[nodiscard]] constexpr Position keyPosition() const noexcept
    {
        return present_ ? Position { node().keyLine, node().keyColumn, 0 } : Position{};
    }

    class iterator
    {
    public:
        constexpr iterator (const View* parent, std::size_t i) noexcept : parent_ (parent), i_ (i) {}
        [[nodiscard]] constexpr View operator*() const noexcept { return (*parent_)[i_]; }
        constexpr iterator& operator++() noexcept { ++i_; return *this; }
        [[nodiscard]] constexpr bool operator== (const iterator& other) const noexcept { return i_ == other.i_; }
    private:
        const View* parent_;
        std::size_t i_;
    };
    [[nodiscard]] constexpr iterator begin() const noexcept { return { this, 0 }; }
    [[nodiscard]] constexpr iterator end() const noexcept { return { this, size() }; }

private:
    const Document* document_ = nullptr;
    std::uint32_t index_ = 0;
    bool present_ = false;
    [[nodiscard]] constexpr const Node& node() const noexcept { return document_->nodes[index_]; }
};

constexpr View Document::root() const noexcept { return View (*this, 0); }

namespace detail
{
// depth counts key components from the root, as the parser does: a table's entries are one deeper than the table,
// and an array's items and the tables of an array of tables are at their key's depth. felitronics_toml2cpp writes
// only documents parse() accepted, so a table at depth 16 has no entries; the recursion, once per level, stops there
// whatever the array holds, and an array's items are never followed into.
[[nodiscard]] inline Value value (View v, std::uint32_t source, std::size_t depth);
[[nodiscard]] inline Table table (View v, std::uint32_t source, std::size_t depth)
{
    Table t;
    t.style = v.isInline() ? Table::Style::Inline : Table::Style::Header;
    t.position = v.position();
    t.position.source = source;
    if (depth >= kMaxDepth) return t;
    for (const View entry : v)
    {
        auto keyPosition = entry.keyPosition();
        keyPosition.source = source;
        (void) t.insert (std::string (entry.key()), value (entry, source, depth + 1), keyPosition);
    }
    return t;
}
inline Value value (View v, std::uint32_t source, std::size_t depth)
{
    Value out;
    if (const auto s = v.string()) out = Value (std::string (*s));
    else if (const auto n = v.integer()) out = Value (*n);
    else if (const auto d = v.decimal()) out = Value (*d);
    else if (const auto b = v.boolean()) out = Value (*b);
    else if (v.is (Type::Table)) out = Value (table (v, source, depth));
    else if (v.is (Type::Array))
    {
        Array items;
        for (const View item : v)
            items.push_back (item.size() != 0 || item.is (Type::Table) || item.is (Type::Array) ? Value{} : value (item, source, depth));
        out = Value (std::move (items));
    }
    else if (v.is (Type::Tables))
    {
        Tables tables;
        for (const View element : v) tables.push_back (table (element, source, depth));
        out = Value (std::move (tables));
    }
    out.position = v.position();
    out.position.source = source;
    return out;
}
}

// The embedded document as the Table parse() gives for its text: the same data, key order, styles and positions,
// with source in every position. It allocates; nothing is parsed.
[[nodiscard]] inline Table toTable (View root, std::uint32_t source = 0)
{
    return detail::table (root, source, 0);
}

} // namespace felitronics::toml::embedded

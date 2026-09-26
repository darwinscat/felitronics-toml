// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

// Where every value, key and table was defined: exact positions for each construct, a sweep over the generated
// documents that finds each position's character without the parser, and the rule that positions never take
// part in equality.
#include <felitronics/toml/Toml.h>
#include "toml_test.h"
#include "Fixtures.h"

using namespace felitronics::toml;

namespace
{
std::string text (Position p)
{
    return fixtures::decimalInteger (p.line) + ":" + fixtures::decimalInteger (p.column) + "@" + fixtures::decimalInteger (p.source);
}
Table parsed (std::string_view document, std::uint32_t source = 0)
{
    auto result = parse (document, source);
    if (auto* t = std::get_if<Table> (&result)) return std::move (*t);
    test::ok (false, "fixture document refused: " + std::string (codeName (std::get<Error> (result).code)));
    return {};
}
// A path of keys and item indexes, walked from the root; a missing step fails the check.
struct Step { std::string key; std::size_t index = 0; bool item = false; };
const Value* walk (const Table& root, std::initializer_list<Step> steps)
{
    const Table* table = &root;
    const Value* value = nullptr;
    for (const auto& s : steps)
    {
        if (s.item)
        {
            if (value == nullptr) return nullptr;
            if (const auto* a = std::get_if<Array> (&value->data)) { if (s.index >= a->size()) return nullptr; value = &(*a)[s.index]; table = nullptr; continue; }
            return nullptr;
        }
        if (table == nullptr) return nullptr;
        value = table->find (s.key);
        if (value == nullptr) return nullptr;
        table = std::get_if<Table> (&value->data);
    }
    return value;
}
void at (const Table& root, std::initializer_list<Step> steps, Position key, Position value, const char* what)
{
    const Value* v = walk (root, steps);
    test::ok (v != nullptr && v->position == value, std::string (what) + ": value at " + text (value) + (v ? ", got " + text (v->position) : ", missing"));
    // The key is the last step's entry in its table.
    const Table* table = &root;
    const Entry* entry = nullptr;
    for (const auto& s : steps)
    {
        if (s.item || table == nullptr) { entry = nullptr; break; }
        entry = table->entry (s.key);
        if (entry == nullptr) break;
        table = std::get_if<Table> (&entry->value.data);
    }
    if (key.line != 0)
        test::ok (entry != nullptr && entry->keyPosition == key, std::string (what) + ": key at " + text (key) + (entry ? ", got " + text (entry->keyPosition) : ", missing"));
}
// Independently of the parser: the character a position points at must be the one its value starts with.
void sweep (std::string_view document, const Table& root, std::uint32_t source)
{
    bool fine = root.position == Position { 1, 1, source };
    std::string why;
    for (const auto& l : fixtures::locate (root))
    {
        if (l.path == "[]") continue;
        if (l.at.source != source || l.at.line == 0) { fine = false; why = l.path + " has no position"; break; }
        if (l.keyed)
        {
            const auto k = document[fixtures::offsetOf (document, l.key)];
            if (l.key.source != source || ! (k == '"' || detail::bare (k))) { fine = false; why = l.path + ": key at " + text (l.key); break; }
        }
    }
    // Walk again with types: strings start with a quote, numbers with a digit or sign, booleans with t or f,
    // arrays with [, header tables with [ and implicit tables at their key.
    std::vector<std::pair<const Table*, std::string>> stack { { &root, "" } };
    while (fine && ! stack.empty())
    {
        const auto* table = stack.back().first;
        const auto path = stack.back().second;
        stack.pop_back();
        for (const auto& e : table->entries())
        {
            const auto& v = e.value;
            const auto c = document[fixtures::offsetOf (document, v.position)];
            const auto ok = [&] (bool condition, const char* kind)
            {
                if (! condition && fine) { fine = false; why = path + "." + e.key + ": " + kind + " at " + text (v.position); }
            };
            if (std::holds_alternative<std::string> (v.data)) ok (c == '"', "string");
            else if (std::holds_alternative<std::int64_t> (v.data) || std::holds_alternative<Decimal> (v.data))
                ok (detail::digit (c) || c == '-' || c == '+', "number");
            else if (const auto* b = std::get_if<bool> (&v.data)) ok (c == (*b ? 't' : 'f'), "boolean");
            else if (const auto* a = std::get_if<Array> (&v.data))
            {
                ok (c == '[', "array");
                for (const auto& item : *a)
                {
                    const auto i = document[fixtures::offsetOf (document, item.position)];
                    if (fine && ! (i == '"' || i == '-' || i == '+' || i == 't' || i == 'f' || detail::digit (i)))
                    { fine = false; why = path + "." + e.key + ": item at " + text (item.position); }
                }
            }
            else if (const auto* t = std::get_if<Table> (&v.data))
            {
                ok (t->position == v.position, "table and value positions agree");
                ok (c == '[' || v.position == e.keyPosition, "table");
                stack.push_back ({ t, path + "." + e.key });
            }
            else if (const auto* ts = std::get_if<Tables> (&v.data))
            {
                ok (c == '[', "array of tables");
                for (const auto& element : *ts)
                {
                    if (fine && document.substr (fixtures::offsetOf (document, element.position), 2) != "[[")
                    { fine = false; why = path + "." + e.key + ": element at " + text (element.position); }
                    stack.push_back ({ &element, path + "." + e.key + "[]" });
                }
            }
        }
    }
    test::ok (fine, "every position points at its value's first character" + (why.empty() ? std::string() : ": " + why));
}
}

int main()
{
    test::group ("each construct at its exact line and column");
    {
        const auto root = parsed ("# a comment\n"
                                  "name = \"Warm\"\n"
                                  "\tgain =  -1.25   # tab before the key\n"
                                  "flags = [ true,\n"
                                  "  false ]\n"
                                  "\"quoted key\".sub = 7\n"
                                  "\n"
                                  "[limiter]\n"
                                  "release = 0.050\n"
                                  "[a.b]\n"
                                  "c = 1\n"
                                  "[a]\n"
                                  "[[bands]]\n"
                                  "f = 100\n"
                                  "[[bands]]\n"
                                  "f = 200\n", 7);
        test::ok (root.position == Position { 1, 1, 7 }, "the root is at 1:1 of its source");
        at (root, { { "name" } }, { 2, 1, 7 }, { 2, 8, 7 }, "string");
        at (root, { { "gain" } }, { 3, 2, 7 }, { 3, 10, 7 }, "decimal after a tab and two spaces");
        at (root, { { "flags" } }, { 4, 1, 7 }, { 4, 9, 7 }, "array");
        at (root, { { "flags" }, { "", 0, true } }, {}, { 4, 11, 7 }, "first item");
        at (root, { { "flags" }, { "", 1, true } }, {}, { 5, 3, 7 }, "item on the next line");
        at (root, { { "quoted key" } }, { 6, 1, 7 }, { 6, 1, 7 }, "a dotted table is where its key first names it");
        at (root, { { "quoted key" }, { "sub" } }, { 6, 14, 7 }, { 6, 20, 7 }, "value under a quoted dotted key");
        at (root, { { "limiter" } }, { 8, 2, 7 }, { 8, 1, 7 }, "a header table is at its [, its key after it");
        at (root, { { "limiter" }, { "release" } }, { 9, 1, 7 }, { 9, 11, 7 }, "value under a header");
        at (root, { { "a" } }, { 10, 2, 7 }, { 12, 1, 7 }, "an implicit table moves to the header that defines it; its key stays where first written");
        at (root, { { "a" }, { "b" } }, { 10, 4, 7 }, { 10, 1, 7 }, "a nested header");
        const auto* bands = root.find ("bands");
        const auto* tables = bands ? std::get_if<Tables> (&bands->data) : nullptr;
        test::ok (bands && bands->position == Position { 13, 1, 7 }, "an array of tables is at its first [[");
        test::ok (tables && tables->size() == 2 && (*tables)[0].position == Position { 13, 1, 7 }
                  && (*tables)[1].position == Position { 15, 1, 7 }, "each element is at its own [[");
        test::ok (tables && (*tables)[1].find ("f") && (*tables)[1].find ("f")->position == Position { 16, 5, 7 }, "a value in the second element");
        test::ok (root.entry ("bands") && root.entry ("bands")->keyPosition == Position { 13, 3, 7 }, "the key of an array of tables");
    }
    {
        // CRLF: the CR is the last column of its line. Columns count code points: Cyrillic, CJK, an emoji and a
        // combining mark before a value each move it by one.
        const auto root = parsed ("a = 1\r\nb = 2\r\n\"" + fixtures::ukraina + "\" = \"日本😀é\" \r\n\"😀\" = [\"é\", \"x\"]");
        at (root, { { "b" } }, { 2, 1, 0 }, { 2, 5, 0 }, "after CRLF");
        at (root, { { fixtures::ukraina } }, { 3, 1, 0 }, { 3, 13, 0 }, "after a Cyrillic key");
        at (root, { { "😀" } }, { 4, 1, 0 }, { 4, 7, 0 }, "after an emoji key");
        at (root, { { "😀" }, { "", 1, true } }, {}, { 4, 13, 0 }, "after a two-byte character in an item");
    }

    test::group ("positions are not part of equality, and caller-built values have none");
    {
        auto a = parsed ("x = 1\n[t]\ny = [1, 2]\n", 1);
        auto b = parsed ("\n\n  x   =   1\n\n[ t ]\n  y = [ 1,\n2 ]\n", 2);
        test::ok (a == b, "the same data at other positions and sources is equal");
        test::ok (a.find ("x")->position != b.find ("x")->position, "while the positions differ");
        Value v (std::int64_t (5));
        test::ok (v.position == Position {} && v.position.line == 0, "a built value has line 0");
        Table t;
        test::ok (t.insert ("k", Value (true), { 3, 4, 5 }) && t.entry ("k")->keyPosition == Position { 3, 4, 5 },
                  "insert() takes a key position");
        test::ok (t.entry ("missing") == nullptr, "entry() is null for a missing key");
        Table positioned;
        positioned.position = { 9, 9, 9 };
        test::ok (Value (positioned).position == Position { 9, 9, 9 }, "a table value takes its table's position");
        Table copy = a, moved = std::move (b);
        test::ok (copy.find ("t")->position == Position { 2, 1, 1 } && moved.position == Position { 1, 1, 2 },
                  "copies and moves keep positions");
    }

    test::group ("every position of every generated document points at its value's first character");
    fixtures::Generator g { 0x95E914F275AE5811ULL };
    for (int i = 0; i < 128; ++i)
    {
        const auto document = write (fixtures::generated (g));
        const auto source = std::uint32_t (i + 1);
        sweep (document, parsed (document, source), source);
    }
    for (const auto& [name, document] : fixtures::unicodeDocuments()) sweep (document, parsed (document, 3), 3);

    test::group ("a megabyte on one line and 65536 lines both locate in linear time");
    std::string line = "a=[";
    for (std::size_t i = 0; i < kMaxArray; ++i) line += "0,";
    line += "]";
    const auto wide = parsed (line);
    const auto* items = std::get_if<Array> (&wide.find ("a")->data);
    test::ok (items && items->back().position == Position { 1, std::uint32_t (4 + 2 * (kMaxArray - 1)), 0 }, "the last item of a long line");
    std::string lines;
    for (std::size_t i = 0; i < kMaxEntries; ++i) lines += "k" + fixtures::decimalInteger (i) + " = 1\n";
    const auto tall = parsed (lines);
    test::ok (tall.entries().back().keyPosition == Position { std::uint32_t (kMaxEntries), 1, 0 }
              && tall.entries().back().value.position == Position { std::uint32_t (kMaxEntries), 10, 0 }, "the last line of a long document");
    return test::report();
}

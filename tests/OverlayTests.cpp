// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

// overlay(base, top): tables merge key by key, everything else is replaced whole, and every value keeps the
// position, source included, of the layer it came from.
#include <felitronics/toml/Toml.h>
#include "toml_test.h"
#include "Fixtures.h"

using namespace felitronics::toml;

namespace
{
Table parsed (std::string_view document, std::uint32_t source)
{
    auto result = parse (document, source);
    if (auto* t = std::get_if<Table> (&result)) return std::move (*t);
    test::ok (false, "fixture document refused: " + std::string (codeName (std::get<Error> (result).code)));
    return {};
}
const Value* at (const Table& t, std::initializer_list<std::string_view> path)
{
    const Table* table = &t;
    const Value* v = nullptr;
    for (const auto key : path)
    {
        if (table == nullptr) return nullptr;
        v = table->find (key);
        if (v == nullptr) return nullptr;
        table = std::get_if<Table> (&v->data);
    }
    return v;
}
std::uint32_t source (const Table& t, std::initializer_list<std::string_view> path)
{
    const auto* v = at (t, path);
    return v ? v->position.source : 0;
}
}

int main()
{
    const std::string_view defaults = "name = \"Warm\"\n"
                                      "gain = 0.0\n"
                                      "tags = [\"a\", \"b\"]\n"
                                      "[limiter]\n"
                                      "ceiling = -1.0\n"
                                      "release = 0.050\n"
                                      "[limiter.detector]\n"
                                      "mode = \"peak\"\n"
                                      "[eq]\n"
                                      "low = 0.0\n"
                                      "[[bands]]\n"
                                      "f = 100\n"
                                      "[[bands]]\n"
                                      "f = 200\n";
    const std::string_view user = "gain = -2.5\n"
                                  "tags = [\"c\"]\n"
                                  "extra = true\n"
                                  "[limiter]\n"
                                  "ceiling = -2.0\n"
                                  "lookahead = 5\n"
                                  "detector.mode = \"rms\"\n"
                                  "[[bands]]\n"
                                  "f = 1000\n";
    const auto base = parsed (defaults, 1), top = parsed (user, 2);
    const auto merged = overlay (base, top);

    test::group ("tables merge key by key, everything else is replaced whole");
    const auto expected = parsed ("name = \"Warm\"\ngain = -2.5\ntags = [\"c\"]\nextra = true\n"
                                  "[limiter]\nceiling = -2.0\nrelease = 0.050\nlookahead = 5\n[limiter.detector]\nmode = \"rms\"\n"
                                  "[eq]\nlow = 0.0\n[[bands]]\nf = 1000\n", 0);
    test::ok (merged == expected, "the merged tree");
    test::ok (write (merged) == "name = \"Warm\"\ngain = -2.5\ntags = [\"c\"]\nextra = true\n\n[limiter]\nceiling = -2.0\n"
                                "release = 0.050\nlookahead = 5\n\n[limiter.detector]\nmode = \"rms\"\n\n[eq]\nlow = 0.0\n\n[[bands]]\nf = 1000\n",
              "base order first, keys only in top appended in top's order");

    test::group ("provenance: every value keeps its own layer's position");
    test::ok (source (merged, { "name" }) == 1 && source (merged, { "gain" }) == 2 && source (merged, { "extra" }) == 2,
              "scalars from base, replaced by top, appended from top");
    test::ok (source (merged, { "tags" }) == 2 && at (merged, { "tags" })->position == Position { 2, 8, 2 }, "an array is top's, at top's position");
    test::ok (source (merged, { "limiter" }) == 1 && at (merged, { "limiter" })->position == Position { 4, 1, 1 },
              "a merged table keeps base's position");
    test::ok (source (merged, { "limiter", "ceiling" }) == 2 && source (merged, { "limiter", "release" }) == 1
              && source (merged, { "limiter", "lookahead" }) == 2 && source (merged, { "limiter", "detector", "mode" }) == 2,
              "inside it each value tells its layer");
    test::ok (merged.entry ("gain")->keyPosition == Position { 1, 1, 2 } && merged.entry ("name")->keyPosition == Position { 1, 1, 1 },
              "a replaced entry takes top's key position");
    const auto* bands = std::get_if<Tables> (&at (merged, { "bands" })->data);
    test::ok (bands && bands->size() == 1 && (*bands)[0].position.source == 2 && (*bands)[0].find ("f")->position.source == 2,
              "an array of tables is replaced whole, its tables with it");

    test::group ("a table meeting a non-table is replaced, both ways; styles and empty layers");
    {
        const auto a = parsed ("t = { x = 1, y = 2 }\ns = 1\nu = [1]\nv = { k = 1 }\n[w]\nk = 1\n", 1);
        const auto b = parsed ("t = { y = 3 }\ns = { z = 2 }\nu = { z = 3 }\nv = 5\n[[w]]\nk = 2\n", 2);
        const auto m = overlay (a, b);
        test::ok (write (m) == "t = { x = 1, y = 3 }\ns = { z = 2 }\nu = { z = 3 }\nv = 5\n\n[[w]]\nk = 2\n",
                  "inline tables merge and stay inline; the rest is top's, with top's style");
        test::ok (std::get<Table> (m.find ("t")->data).style == Table::Style::Inline && source (m, { "t", "y" }) == 2 && source (m, { "t", "x" }) == 1,
                  "the merged inline table keeps base's style");
        test::ok (overlay (a, Table{}) == a && write (overlay (a, Table{})) == write (a), "an empty top changes nothing");
        test::ok (overlay (Table{}, b) == b && write (overlay (Table{}, b)) == write (b), "an empty base gives top");
        const auto self = overlay (a, a);
        test::ok (self == a && write (self) == write (a), "a tree over itself is itself");
        Table h, i;
        h.style = Table::Style::Header;
        i.style = Table::Style::Inline;
        Table x, y;
        fixtures::put (x, "t", h);
        fixtures::put (y, "t", i);
        test::ok (std::get<Table> (overlay (x, y).find ("t")->data).style == Table::Style::Header, "base's style wins when tables merge");
    }

    test::group ("layers apply in order, and the inputs are untouched");
    {
        const auto c = parsed ("gain = 1.0\n[limiter]\nrelease = 0.1\n", 3);
        const auto three = overlay (overlay (base, top), c);
        test::ok (source (three, { "gain" }) == 3 && source (three, { "limiter", "release" }) == 3
                  && source (three, { "limiter", "ceiling" }) == 2 && source (three, { "name" }) == 1, "three layers, each value from its own");
        test::ok (base == parsed (defaults, 0) && top == parsed (user, 0), "base and top are unchanged");
    }

    test::group ("trees deeper and wider than any document");
    {
        // Copies and destructors of a tree recurse once per level, and a Windows main thread has 1 MB of stack, which
        // a Debug build with ASan fills in a few hundred levels: so 200, still far past any document's 16. Merging
        // itself keeps its own stack.
        Table deepBase, deepTop;
        Table* b = &deepBase;
        Table* t = &deepTop;
        for (int i = 0; i < 200; ++i)
        {
            fixtures::put (*b, "b" + fixtures::decimalInteger (std::uint64_t (i % 7)), std::int64_t (i));
            fixtures::put (*b, "n", Table{});
            fixtures::put (*t, "n", Table{});
            b = std::get_if<Table> (&b->find ("n")->data);
            t = std::get_if<Table> (&t->find ("n")->data);
        }
        fixtures::put (*t, "leaf", true);
        const auto m = overlay (deepBase, deepTop);
        const Table* walk = &m;
        int depth = 0;
        while (const auto* next = walk->find ("n")) { walk = std::get_if<Table> (&next->data); ++depth; }
        test::ok (depth == 200 && walk->find ("leaf") != nullptr && walk->find ("b5") == nullptr, "a 200-level caller-built tree");
        Table wideBase, wideTop;
        for (std::uint64_t i = 0; i < 70000; ++i)
        {
            fixtures::put (wideBase, "k" + fixtures::decimalInteger (i), std::int64_t (i));
            fixtures::put (wideTop, "k" + fixtures::decimalInteger (i * 2), std::int64_t (-1));
        }
        const auto m2 = overlay (wideBase, wideTop);
        test::ok (m2.entries().size() == 105000 && ! writeChecked (m2), "the union may exceed the entry limit, and the writer refuses it");
    }
    return test::report();
}

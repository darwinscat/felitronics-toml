// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#include <felitronics/toml/Toml.h>
#include "toml_test.h"
#include "Fixtures.h"
#include <cstdio>

using namespace felitronics::toml;

namespace
{
void check (const Table& tree, bool dump, std::uint64_t& digest)
{
    const auto text = writeChecked (tree);
    if (! text) { if (dump) std::abort(); test::ok (false, "generated tree refused"); return; }
    if (dump)
    {
        const auto record = "{\"toml\":" + fixtures::jsonString (*text) + ",\"expected\":" + fixtures::expectedTable (tree) + "}\n";
        if (std::fwrite (record.data(), 1, record.size(), stdout) != record.size()) std::abort();
        return;
    }
    for (const char c : *text) { digest ^= static_cast<unsigned char> (c); digest *= 1099511628211ULL; }
    const auto result = parse (*text);
    const auto* parsed = std::get_if<Table> (&result);
    test::ok (parsed && *parsed == tree, "parse(write(tree)) == tree, including decimal scale and sign");
    test::ok (parsed && write (*parsed) == *text, "write(parse(canonical)) == canonical byte for byte");
    if (parsed) test::ok (fixtures::expectedTable (tree).size() == fixtures::expectedTable (*parsed).size(), "all metadata retained");
}
}
int main (int argc, char** argv)
{
    const bool dump = argc == 2 && std::string_view (argv[1]) == "--dump";
    fixtures::Generator g { 0x95E914F275AE5811ULL };
    std::uint64_t digest = 14695981039346656037ULL;
    for (int i = 0; i < 512; ++i) check (fixtures::generated (g), dump, digest);
    Table depth;
    for (int i = 0; i < 16; ++i) { Table parent; fixtures::put (parent, "a", std::move (depth)); depth = std::move (parent); }
    check (depth, dump, digest);
    check (Table{}, dump, digest);
    if (dump) return 0;
    test::group ("writer grouping, key quoting, exact decimal spelling and insertion order");
    Table root, sub;
    fixtures::put (sub, "z", false);
    fixtures::put (root, "table", std::move (sub));
    fixtures::put (root, "b", std::int64_t (1));
    fixtures::put (root, "array", Tables { Table{} });
    fixtures::put (root, "a", Decimal { 0, 2, true });
    fixtures::put (root, "", "é\n\\\"");
    // The string holds a line feed, so it is written on several lines; its last quote is escaped.
    test::ok (write (root) == "b = 1\na = -0.00\n\"\" = \"\"\"\né\n\\\\\\\"\"\"\"\n\n[table]\nz = false\n\n[[array]]\n", "canonical bytes");
    test::ok (root.entries()[0].key == "table" && root.entries()[1].key == "b", "caller insertion order remains intact");
    test::ok (! root.insert ("b", true), "public insertion refuses duplicates");
    Table copy = root;
    test::ok (copy == root && write (copy) == write (root), "copy includes a valid independent lookup index");
    Table moved = std::move (copy);
    test::ok (moved.find ("table") != nullptr && moved == root, "moved lookup index follows its entries");

    test::ok (copy.find ("table") == nullptr && copy.insert ("reused", true), "moved-from table remains reusable");

    test::group ("writer refuses unrepresentable caller trees without recursive array descent");
    for (Value invalid : { Value (Decimal { 1, 0 }), Value (Decimal { Decimal::kMaxMantissa + 1, 1 }),
            Value (Decimal { 1, 1, true }), Value (Tables{}), Value (Array { Value (true), Value (std::int64_t (1)) }),
            Value (Array { Value (Array{}) }), Value (Array { Value (Table{}) }), Value (std::string ("\x80")),
            Value (std::string (65537, 'x')), Value (Array (65537, Value (false))) })
    {
        Table t; fixtures::put (t, "a", std::move (invalid));
        test::ok (! writeChecked (t), "invalid constructed value refused");
    }
    Table tooDeep;
    fixtures::put (tooDeep, "extra", std::move (depth));
    test::ok (! writeChecked (tooDeep), "writer depth bounded at 16");
    for (const auto& key : { std::string (257, 'x'), std::string ("\x80") })
    {
        Table t; fixtures::put (t, key, true);
        test::ok (! writeChecked (t), "invalid constructed key refused");
    }
    // Adversarial sorted, reverse-sorted and alternating keys exercise all AVL rotations, then copies.
    for (int order = 0; order < 3; ++order)
    {
        Table t;
        for (int i = 0; i < 8192; ++i)
        {
            const auto n = order == 0 ? i : order == 1 ? 8191 - i : (i % 2 ? i / 2 : 8191 - i / 2);
            fixtures::put (t, fixtures::decimalInteger (std::uint64_t (n)), std::int64_t (n));
        }
        Table copied = t;
        for (std::uint64_t i = 0; i < 8192; ++i)
        {
            const auto* value = copied.find (fixtures::decimalInteger (i));
            test::ok (value && std::get<std::int64_t> (value->data) == std::int64_t (i), "indexed lookup returns the inserted value");
        }
    }
    // The digest is reported as decimal without a floating or locale formatter; native/wasm must match.
    const auto text = "canonical corpus FNV-1a: " + fixtures::decimalInteger (digest) + "\n";
    (void) std::fwrite (text.data(), 1, text.size(), stdout);
    return test::report();
}

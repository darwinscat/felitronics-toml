// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

// The README example. ctest builds and runs it twice, normally and under -fno-exceptions -fno-rtti
// (MSVC: /EHs-c- /GR- with _HAS_EXCEPTIONS=0), and compares its output byte for byte.
#include <felitronics/toml/Toml.h>
#include <cstdio>

using namespace felitronics::toml;

int main()
{
    auto result = parse ("name = \"Warm master\"\n"
                         "ceiling = -1.000  # dBTP\n"
                         "\n"
                         "[limiter]\n"
                         "release = 0.050\n");
    if (const Error* e = std::get_if<Error> (&result))
    {
        // A stable code and a 1-based line:column, the column counted in characters. Your UI owns the message.
        std::printf ("%s at %u:%u\n", codeName (e->code), unsigned (e->line), unsigned (e->column));
        return 1;
    }
    Table& root = *std::get_if<Table> (&result);

    // find() is null for a missing key, get_if is null for another type. No exceptions anywhere.
    const Value* ceiling = root.find ("ceiling");
    const Decimal* d = ceiling ? std::get_if<Decimal> (&ceiling->data) : nullptr;
    if (d == nullptr) return 1;
    std::printf ("ceiling = %lld / 10^%u\n", static_cast<long long> (d->mantissa), unsigned (d->scale));
    // d->toDouble() is -1.0: one correctly rounded division, the same bits on every platform.

    // insert() appends and refuses a key that already exists. fromDouble() quantizes to a chosen scale.
    if (! root.insert ("gain", Decimal::fromDouble (0.5, 2))) return 1;

    // Canonical text: scalars first, then tables, each group in insertion order. Comments are not kept.
    const std::string text = write (root);
    std::fwrite (text.data(), 1, text.size(), stdout);
    return 0;
}

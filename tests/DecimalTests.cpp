// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#include <felitronics/toml/Toml.h>
#include "toml_test.h"
#include <bit>
#include <cmath>

using namespace felitronics::toml;

namespace
{
// Independent rational-to-binary64 oracle. Normalise using integer shifts, then perform 52 steps of
// binary long division. The remainder decides nearest/even. No floating-point arithmetic, long double,
// conversion library or compiler-specific wide integer participates in the expected bits.
std::uint64_t rationalBits (std::int64_t mantissa, std::uint8_t scale, bool negativeZero = false)
{
    const auto sign = mantissa < 0 || negativeZero ? std::uint64_t (1) << 63 : 0;
    std::uint64_t n = mantissa < 0 ? std::uint64_t (-mantissa) : std::uint64_t (mantissa);
    if (n == 0) return sign;
    std::uint64_t d = 1;
    for (unsigned i = 0; i < scale; ++i) d *= 10;
    int exponent = 0;
    while (n < d) { n *= 2; --exponent; }
    while (n >= 2 * d) { d *= 2; ++exponent; }
    std::uint64_t significand = 1, remainder = n - d;
    for (int i = 0; i < 52; ++i)
    {
        remainder *= 2;
        significand <<= 1;
        if (remainder >= d) { ++significand; remainder -= d; }
    }
    if (2 * remainder > d || (2 * remainder == d && (significand & 1))) ++significand;
    if (significand == (std::uint64_t (1) << 53)) { significand >>= 1; ++exponent; }
    return sign | (std::uint64_t (exponent + 1023) << 52) | (significand & ((std::uint64_t (1) << 52) - 1));
}
void check (std::int64_t m, std::uint8_t s, bool negativeZero = false)
{
    const Decimal d { m, s, negativeZero };
    test::ok (d.valid() && std::bit_cast<std::uint64_t> (d.toDouble()) == rationalBits (m, s, negativeZero),
              "division bits equal the independent exact rational oracle");
}
}
int main()
{
    test::group ("every scale, signed edges, halfway cases and 90000 seeded exact rationals");
    std::uint64_t state = 0xBC257BA391E15244ULL;
    for (std::uint8_t s = 1; s <= 9; ++s)
    {
        for (const std::int64_t m : { std::int64_t (0), std::int64_t (1), std::int64_t (5), std::int64_t (125),
                Decimal::kMaxMantissa / 2 - 1, Decimal::kMaxMantissa / 2,
                Decimal::kMaxMantissa - 1, Decimal::kMaxMantissa })
        { check (m, s); check (-m, s); }
        check (0, s, true);
        for (int i = 0; i < 10000; ++i)
        {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            const auto m = std::int64_t (state & ((std::uint64_t (1) << 53) - 1));
            check ((state >> 63) ? -m : m, s);
        }
    }
    // x87 arithmetic rounds this quotient to 64 bits and then to 53, one unit in the last place low (Toml.h asserts it away).
    check (7832510068573872, 8);
    test::group ("fromDouble rounds the binary64 product half away, or explicitly refuses");
    test::ok (Decimal::fromDouble (1.25, 1) == Decimal { 13, 1 }, "positive halfway");
    test::ok (Decimal::fromDouble (-1.25, 1) == Decimal { -13, 1 }, "negative halfway");
    test::ok (Decimal::fromDouble (std::nextafter (1.25, 0.0), 1) == Decimal { 12, 1 }, "below halfway");
    test::ok (Decimal::fromDouble (std::nextafter (1.25, 2.0), 1) == Decimal { 13, 1 }, "above halfway");
    test::ok (Decimal::fromDouble (-0.0, 9) == Decimal { 0, 9, true }, "negative zero preserved");
    test::ok (Decimal::fromDouble (-0.001, 1) == Decimal { 0, 1, true }, "rounding to negative zero");
    test::ok (Decimal::fromDouble (0.0, 1) == Decimal { 0, 1, false }, "positive zero preserved");
    test::ok (! Decimal::fromDouble (1, 0).valid() && ! Decimal::fromDouble (1, 10).valid(), "invalid scales refused");
    for (double x : { std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
                     std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::max(), 1e20, -1e20 })
        test::ok (! Decimal::fromDouble (x, 1).valid(), "nonfinite and out of range refused before casting");
    for (std::uint8_t s = 1; s <= 9; ++s)
    {
        double factor = 1;
        for (unsigned i = 0; i < s; ++i) factor *= 10;
        const double edge = double (Decimal::kMaxMantissa) / factor;
        test::ok (Decimal::fromDouble (edge, s).valid(), "mantissa bound included");
        test::ok (! Decimal::fromDouble (std::nextafter (std::nextafter (edge, std::numeric_limits<double>::infinity()), std::numeric_limits<double>::infinity()), s).valid(),
                  "mantissa bound exceeded");
    }
    for (const Decimal d : { Decimal { Decimal::kMaxMantissa + 1, 1 }, Decimal { -Decimal::kMaxMantissa - 1, 1 },
                           Decimal { 0, 0 }, Decimal { 0, 10 }, Decimal { 1, 1, true } })
        test::ok (! d.valid() && std::isnan (d.toDouble()), "invalid aggregate cannot perform an inexact division");
    return test::report();
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

// Typed reading (Schema.h): fields into the caller's structs, with types, ranges, defaults and requirements; every
// problem a code, a key path and a position; keys nobody read reported; integers read as decimals losslessly.
#include <felitronics/toml/Schema.h>
#include "toml_test.h"
#include "Fixtures.h"

using namespace felitronics::toml;

namespace
{
Table parsed (std::string_view document, std::uint32_t source = 0)
{
    auto result = parse (document, source);
    if (auto* t = std::get_if<Table> (&result)) return std::move (*t);
    test::ok (false, "fixture document refused: " + std::string (codeName (std::get<Error> (result).code)));
    return {};
}
std::string spelled (const Report& report)
{
    std::string out;
    for (const auto& p : report.problems)
        out += std::string (faultName (p.fault)) + (p.severity == Severity::Error ? " " : " (warning) ") + p.path + " "
             + fixtures::decimalInteger (p.position.line) + ":" + fixtures::decimalInteger (p.position.column)
             + (p.detail ? " #" + fixtures::decimalInteger (p.detail) : "") + "\n";
    return out;
}
// Bit for bit: the suites build with -Wfloat-equal.
bool same (double x, double y) { return std::bit_cast<std::uint64_t> (x) == std::bit_cast<std::uint64_t> (y); }
void problems (const Report& report, const std::string& expected, const char* what)
{
    const auto got = spelled (report);
    test::ok (got == expected, std::string (what) + (got == expected ? "" : ":\n      expected:\n" + expected + "      got:\n" + got));
}

// The README's shape: an application's own structs, and one function per struct.
struct Band { std::int64_t frequency = 0; Decimal gain; std::string type = "peak"; };
struct Limiter { double ceiling = -1.0; int lookaheadMs = 5; };
struct Settings
{
    std::string name;
    Decimal gain;
    std::uint32_t sampleRate = 48000;
    bool dither = false;
    std::vector<std::int64_t> taps;
    Limiter limiter;
    std::vector<Band> bands;
};
void readLimiter (Reader& in, Limiter& l)
{
    in.required ("ceiling", l.ceiling, { -12.0, 0.0 });
    in.optional ("lookahead", l.lookaheadMs, { 0, 50 });
}
void readBand (Reader& in, Band& b)
{
    in.required ("frequency", b.frequency, { 20, 20000 });
    in.required ("gain", b.gain, { Decimal { -240, 1 }, Decimal { 240, 1 } });
    if (in.optional ("type", b.type) && b.type != "peak" && b.type != "shelf") in.refuse ("type", 7);
}
Report readSettings (const Table& root, Settings& s, ReadOptions options = {})
{
    return read (root, [&] (Reader& in)
    {
        in.required ("name", s.name);
        in.required ("gain", s.gain, { Decimal { -120, 1 }, Decimal { 120, 1 } });
        in.optional ("sample-rate", s.sampleRate, { 8000u, 192000u });
        in.optional ("dither", s.dither);
        in.optional ("taps", s.taps, { 0, 255 });
        in.table ("limiter", Need::Required, [&] (Reader& t) { readLimiter (t, s.limiter); });
        in.tables ("bands", Need::Optional, [&] (Reader& row) { Band b; readBand (row, b); s.bands.push_back (b); });
    }, options);
}
}

int main()
{
    test::group ("a whole document into the application's structs");
    {
        const auto root = parsed ("name = \"Warm\"\n"
                                  "gain = -3            # an integer where a decimal is expected: -3.0\n"
                                  "taps = [1, 2, 3]\n"
                                  "limiter = { ceiling = -1.5 }\n"
                                  "bands = [\n"
                                  "  { frequency = 100, gain = 1.25 },\n"
                                  "  { frequency = 8_000, gain = -2, type = \"shelf\" },\n"
                                  "]\n");
        Settings s;
        const auto report = readSettings (root, s);
        test::ok (report.ok() && report.problems.empty(), "no problems:\n" + spelled (report));
        test::ok (s.name == "Warm" && s.gain == Decimal { -30, 1 } && s.sampleRate == 48000 && ! s.dither
                  && s.taps == std::vector<std::int64_t> { 1, 2, 3 }, "fields, an integer read as n.0, defaults kept");
        test::ok (same (s.limiter.ceiling, -1.5) && s.limiter.lookaheadMs == 5, "a sub-table, a double, an optional int's default");
        test::ok (s.bands.size() == 2 && s.bands[1].frequency == 8000 && s.bands[1].gain == Decimal { -20, 1 }
                  && s.bands[1].type == "shelf" && s.bands[0].type == "peak", "an array of inline tables, row by row");
        // The same data with headers reads the same.
        const auto headers = parsed ("name = \"Warm\"\ngain = -3.0\ntaps = [1, 2, 3]\n[limiter]\nceiling = -1.5\n"
                                     "[[bands]]\nfrequency = 100\ngain = 1.25\n[[bands]]\nfrequency = 8000\ngain = -2.0\ntype = \"shelf\"\n");
        Settings h;
        test::ok (readSettings (headers, h).ok() && h.bands.size() == 2 && h.bands[1].gain == s.bands[1].gain
                  && h.gain == s.gain && same (h.limiter.ceiling, s.limiter.ceiling), "headers and inline tables read alike");
    }

    test::group ("problems: a code, a key path, a position, and the field left as it was");
    {
        const auto root = parsed ("name = 5\n"                               // 1: WrongType at 1:8
                                  "gain = 12.000000001\n"                    // 2: OutOfRange, exactly
                                  "sample-rate = 4_000_000_000_000\n"        // 3: does not fit uint32 and its range
                                  "taps = [1, 256, -1]\n"                    // 4: the first bad item
                                  "\"odd key\" = 1\n"                        // 5: unknown, quoted in its path
                                  "[limiter]\n"                              // 6: lacks ceiling
                                  "lookahead = \"5\"\n"                      // 7: WrongType
                                  "[[bands]]\n"                              // 8: lacks frequency
                                  "gain = 30\n"                              // 9: 30.0 is out of range
                                  "type = \"notch\"\n"                       // 10: Refused, detail 7
                                  "gian = 1.0\n");                           // 11: a typo
        Settings s;
        s.name = "unchanged";
        const auto report = readSettings (root, s);
        problems (report, "WrongType name 1:8\n"
                          "OutOfRange gain 2:8\n"
                          "OutOfRange sample-rate 3:15\n"
                          "OutOfRange taps[1] 4:12\n"
                          "Missing limiter.ceiling 6:1\n"
                          "WrongType limiter.lookahead 7:13\n"
                          "Missing bands[0].frequency 8:1\n"
                          "OutOfRange bands[0].gain 9:8\n"
                          "Refused bands[0].type 10:8 #7\n"
                          "UnknownKey bands[0].gian 11:1\n"
                          "UnknownKey \"odd key\" 5:1\n", "every problem in the order it was found");
        test::ok (! report.ok(), "errors make the report not ok");
        test::ok (s.name == "unchanged" && s.sampleRate == 48000 && s.taps.empty() && s.limiter.lookaheadMs == 5,
                  "a refused field keeps the value it had");
        Settings missing;
        problems (readSettings (Table{}, missing), "Missing name 0:0\nMissing gain 0:0\nMissing limiter 0:0\n",
                  "a built root has no position; a parsed root is at 1:1");
        problems (readSettings (parsed ("\n"), missing), "Missing name 1:1\nMissing gain 1:1\nMissing limiter 1:1\n", "a parsed root");
    }

    test::group ("unknown keys: an error or a warning, the caller's choice");
    {
        const auto root = parsed ("nmae = \"x\"\n[limitr]\nceiling = 1.0\n[limiter]\nceilng = -1.0\nceiling = -1.0\n");
        Settings s;
        const auto strict = readSettings (root, s);
        problems (strict, "Missing name 1:1\nMissing gain 1:1\nUnknownKey limiter.ceilng 5:1\nUnknownKey nmae 1:1\nUnknownKey limitr 2:2\n",
                  "keys nobody read, sub-tables first as each finishes, then the root's in entry order");
        Settings w;
        const auto lenient = readSettings (parsed ("name = \"x\"\ngain = 0.0\nextra = 1\n[limiter]\nceiling = -1.0\nmore = true\n"), w,
                                           { Severity::Warning });
        problems (lenient, "UnknownKey (warning) limiter.more 6:1\nUnknownKey (warning) extra 3:1\n", "as warnings");
        test::ok (lenient.ok() && w.name == "x" && same (w.limiter.ceiling, -1.0), "warnings leave the report ok, and the rest read");
        // A key read in any way is known: a failed read, field(), or refuse() does not add UnknownKey.
        const auto doc = parsed ("a = \"x\"\nb = 1\nc = 2\n");
        const auto r = read (doc, [] (Reader& in)
        {
            bool flag = false;
            in.required ("a", flag);
            test::ok (in.field ("b", Need::Required) != nullptr && in.field ("zz", Need::Optional) == nullptr, "field() gives the raw value");
            in.finish();
            in.finish();
        });
        problems (r, "WrongType a 1:5\nUnknownKey c 3:1\n", "finish() reports once");
    }

    test::group ("integers read as decimals: n is n.0, exactly, while it fits the mantissa");
    {
        test::ok (asDecimal (Value (std::int64_t (5))) == Decimal { 50, 1 } && asDecimal (Value (std::int64_t (-5))) == Decimal { -50, 1 },
                  "5 is 5.0 and -5 is -5.0");
        test::ok (asDecimal (Value (std::int64_t (0))) == Decimal { 0, 1, false }, "0 is 0.0, never a negative zero");
        test::ok (asDecimal (Value (std::int64_t (900719925474099))) == Decimal { 9007199254740990, 1 }
                  && asDecimal (Value (std::int64_t (-900719925474099))) == Decimal { -9007199254740990, 1 }, "the largest that fit");
        test::ok (! asDecimal (Value (std::int64_t (900719925474100))) && ! asDecimal (Value (std::numeric_limits<std::int64_t>::min())),
                  "one more does not");
        test::ok (asDecimal (Value (Decimal { 125, 3 })) == Decimal { 125, 3 } && ! asDecimal (Value (true)) && ! asDecimal (Value ("1")),
                  "a decimal is itself; other types are none");
        const auto doc = parsed ("a = 900719925474100\nb = [1, 2]\nc = 7\nd = true\n");
        Decimal a;
        std::vector<Decimal> b;
        double c = 0;
        const auto r = read (doc, [&] (Reader& in)
        {
            test::ok (! in.required ("a", a), "too large for a decimal");
            test::ok (in.required ("b", b) && b == std::vector<Decimal> { Decimal { 10, 1 }, Decimal { 20, 1 } }, "an integer array as decimals");
            test::ok (in.required ("c", c) && same (c, 7.0), "an integer as a double");
            test::ok (! in.required ("d", c), "a boolean is no number");
        });
        problems (r, "OutOfRange a 1:5\nWrongType d 4:5\n", "the integer that does not fit is out of range");
    }

    test::group ("ranges: inclusive, exact for decimals whatever the scale, in the field's own type");
    {
        const auto doc = parsed ("d1 = 1.50\nd2 = -0.0\nd3 = 0.000000001\nd4 = -12.000000000\nd5 = 900719925.4740992\n"
                                 "i1 = 127\ni2 = 128\ni3 = -1\ni4 = 20\nf1 = 0.1\nf2 = 1\n");
        const auto r = read (doc, [] (Reader& in)
        {
            Decimal d;
            test::ok (in.required ("d1", d, { Decimal { 15, 1 }, Decimal { 15, 1 } }), "1.50 is within [1.5, 1.5]");
            test::ok (in.required ("d2", d, { Decimal { 0, 3 }, Decimal { 0, 9 } }) && d.negativeZero, "-0.0 is within [0.000, 0.000000000]");
            test::ok (! in.required ("d3", d, { std::nullopt, Decimal { 0, 1 } }), "0.000000001 is above 0.0");
            test::ok (! in.required ("d4", d, { Decimal { -12, 1 } }), "-12.000000000 is below -1.2");
            test::ok (in.required ("d5", d, { Decimal { -Decimal::kMaxMantissa, 7 }, Decimal { Decimal::kMaxMantissa, 7 } }),
                      "the largest mantissas at different scales");
            std::int8_t small = 0;
            std::uint16_t unsigned16 = 0;
            test::ok (in.required ("i1", small) && small == 127, "127 fits int8");
            test::ok (! in.required ("i2", small) && small == 127, "128 does not");
            test::ok (! in.required ("i3", unsigned16), "-1 is no uint16");
            test::ok (! in.required ("i4", unsigned16, { std::uint16_t (21) }), "20 is below 21");
            double f = 0;
            test::ok (in.required ("f1", f, { 0.1, 0.1 }) && same (f, 0.1), "a decimal as the double it rounds to");
            test::ok (! in.required ("f2", f, { std::nullopt, 0.5 }), "1 is above 0.5");
            Decimal bad;
            test::ok (! in.optional ("d1", bad, { Decimal { 1, 0 } }), "an invalid bound refuses every value");
        });
        problems (r, "OutOfRange d3 3:6\nOutOfRange d4 4:6\nOutOfRange i2 7:6\nOutOfRange i3 8:6\nOutOfRange i4 9:6\n"
                     "OutOfRange f2 11:6\nOutOfRange d1 1:6\n", "each at its value");
        const auto compare = [] (Decimal x, Decimal y) { return detail::compare (x, y); };
        test::ok (compare ({ 15, 1 }, { 150, 2 }) == 0 && compare ({ 0, 1, true }, { 0, 9 }) == 0 && compare ({ -1, 9 }, { 0, 1 }) < 0
                  && compare ({ 1, 9 }, { 0, 1, true }) > 0 && compare ({ Decimal::kMaxMantissa, 1 }, { Decimal::kMaxMantissa, 9 }) > 0
                  && compare ({ -Decimal::kMaxMantissa, 1 }, { -Decimal::kMaxMantissa, 9 }) < 0
                  && compare ({ 9007199254740990, 9 }, { 900719925474099, 8 }) == 0
                  && compare ({ 9007199254740991, 9 }, { 900719925474099, 8 }) > 0, "exact comparison across scales and signs");
    }

    test::group ("containers: tables, arrays of tables, arrays; paths spelled as TOML keys");
    {
        const auto doc = parsed ("t = 5\nrows = { a = 1 }\nempty = []\nscalars = [1]\nlist = 3\n"
                                 "\"a b\" = { \"c.d\" = { x = 1 } }\n[[\"" + fixtures::kyiv + "\"]]\ny = 1\n");
        int calls = 0;
        const auto r = read (doc, [&] (Reader& in)
        {
            test::ok (! in.table ("t", Need::Required, [&] (Reader&) { ++calls; }), "a scalar is no table");
            test::ok (! in.tables ("rows", Need::Required, [&] (Reader&) { ++calls; }), "a table is no array of tables");
            test::ok (in.tables ("empty", Need::Required, [&] (Reader&) { ++calls; }), "[] is an array of no tables");
            test::ok (! in.tables ("scalars", Need::Required, [&] (Reader&) { ++calls; }), "a scalar array is not");
            std::vector<std::int64_t> list { 9 };
            test::ok (! in.required ("list", list) && list == std::vector<std::int64_t> { 9 }, "a scalar is no array");
            in.table ("a b", Need::Required, [] (Reader& ab)
            {
                test::ok (ab.path() == "\"a b\"", "a quoted key in a path");
                ab.table ("c.d", Need::Required, [] (Reader& cd) { test::ok (cd.path() == "\"a b\".\"c.d\"", "a dot inside a quoted key"); });
            });
            in.tables (fixtures::kyiv, Need::Required, [] (Reader& row) { test::ok (row.path() == "\"" + fixtures::kyiv + "\"[0]", "a row"); });
            test::ok (! in.tables ("absent", Need::Optional, [&] (Reader&) { ++calls; }), "an absent optional array of tables");
        });
        test::ok (calls == 0, "no callback for a wrong container");
        problems (r, "WrongType t 1:5\nWrongType rows 2:8\nWrongType scalars 4:11\nWrongType list 5:8\n"
                     "UnknownKey \"a b\".\"c.d\".x 6:21\nUnknownKey \"" + fixtures::kyiv + "\"[0].y 8:1\n", "wrong containers, and the unread keys inside");
    }

    test::group ("the caller's own checks, with its own codes");
    {
        const auto doc = parsed ("mode = \"loud\"\n[t]\n");
        const auto r = read (doc, [] (Reader& in)
        {
            std::string mode;
            if (in.required ("mode", mode) && mode != "soft") in.refuse ("mode", 42);
            in.table ("t", Need::Required, [] (Reader& t) { t.refuse ("absent", 3); });
        });
        problems (r, "Refused mode 1:8 #42\nRefused t.absent 2:1 #3\n", "at the value, or at the table when the key is absent");
        test::ok (std::string_view (faultName (Fault::Refused)) == "Refused" && std::string_view (faultName (Fault (99))) == "Unknown",
                  "stable fault names");
    }

    test::group ("layers: a problem's position names the layer that set the value");
    {
        const auto defaults = parsed ("name = \"Warm\"\ngain = 0.0\n[limiter]\nceiling = -1.0\n", 1);
        const auto user = parsed ("gain = 99.0\n[limiter]\nlookahead = 500\n", 2);
        Settings s;
        const auto report = readSettings (overlay (defaults, user), s);
        test::ok (report.problems.size() == 2 && report.problems[0].path == "gain" && report.problems[0].position == Position { 1, 8, 2 }
                  && report.problems[1].path == "limiter.lookahead" && report.problems[1].position.source == 2,
                  "both out of range values came from the user's file:\n" + spelled (report));
    }
    return test::report();
}

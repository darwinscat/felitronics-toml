// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.

// The factory preset is constexpr data; a user's file is laid over it, and the result is read into structs.
#include "factory.h"
#include <felitronics/toml/Schema.h>
#include <cstdio>
#include <vector>

using namespace felitronics::toml;

struct Band { std::int64_t frequency = 0; Decimal gain; };
struct Preset { std::string name; double ceiling = 0; std::vector<Band> bands; };

static_assert (presets::factory.root().find ("bands").size() == 2);

int main()
{
    const Table factory = embedded::toTable (presets::factory.root(), 1);
    auto user = parse ("ceiling = -1.5\ncolour = \"red\"\n", 2);
    if (! std::holds_alternative<Table> (user)) return 1;
    const Table settings = overlay (factory, std::get<Table> (user));

    Preset preset;
    const Report report = read (settings, [&] (Reader& in)
    {
        in.required ("name", preset.name);
        in.required ("ceiling", preset.ceiling, { -12.0, 0.0 });
        in.tables ("bands", Need::Required, [&] (Reader& row)
        {
            Band band;
            row.required ("frequency", band.frequency, { 20, 20000 });
            row.required ("gain", band.gain, { Decimal { -240, 1 }, Decimal { 240, 1 } });
            preset.bands.push_back (band);
        });
    }, { Severity::Warning });
    if (! report.ok()) return 1;
    std::printf ("ceiling %.1f from layer %u, %zu bands, %zu warning\n", preset.ceiling,
                 unsigned (settings.find ("ceiling")->position.source), preset.bands.size(), report.problems.size());
    return 0;
}

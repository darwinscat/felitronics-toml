// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#include <felitronics/toml/Toml.h>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput (const std::uint8_t* data, std::size_t size)
{
    using namespace felitronics::toml;
    auto result = parse ({ reinterpret_cast<const char*> (data), size });
    if (const auto* root = std::get_if<Table> (&result))
    {
        const auto text = writeChecked (*root);
        if (! text) std::abort();
        const auto again = parse (*text);
        const auto* tree = std::get_if<Table> (&again);
        if (tree == nullptr || ! (*tree == *root) || write (*tree) != *text) std::abort();
    }
    return 0;
}

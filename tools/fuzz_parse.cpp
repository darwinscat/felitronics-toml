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
        // Every value has a position in the document; a layer laid over an equal tree changes nothing.
        std::vector<const Table*> tables { root };
        while (! tables.empty())
        {
            const Table* t = tables.back();
            tables.pop_back();
            if (t->position.line == 0 || t->position.column == 0) std::abort();
            for (const auto& e : t->entries())
            {
                if (e.keyPosition.line == 0 || e.value.position.line == 0 || e.value.position.column == 0) std::abort();
                if (const auto* child = std::get_if<Table> (&e.value.data)) tables.push_back (child);
                else if (const auto* array = std::get_if<Tables> (&e.value.data))
                    for (const auto& element : *array) tables.push_back (&element);
            }
        }
        const auto layered = overlay (*root, *tree);
        if (! (layered == *root) || write (layered) != *text) std::abort();
    }
    return 0;
}

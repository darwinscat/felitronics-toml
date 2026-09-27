// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#include <felitronics/toml/Schema.h>
#include "Fixtures.h"
#include "toml_test.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#ifdef _MSC_VER
#include <malloc.h>
#endif

namespace allocation
{
bool active = false;
std::size_t bytes = 0;
void* get (std::size_t n)
{
    if (active) bytes += n;
    if (void* p = std::malloc (n == 0 ? 1 : n)) return p;
    std::abort();
}
void* aligned (std::size_t n, std::size_t alignment)
{
    if (active) bytes += n;
#ifdef _MSC_VER
    void* p = _aligned_malloc (n == 0 ? 1 : n, alignment);
#else
    void* p = nullptr;
    if (posix_memalign (&p, alignment, n == 0 ? 1 : n) != 0) std::abort();
#endif
    if (p == nullptr) std::abort();
    return p;
}
void freeAligned (void* p)
{
#ifdef _MSC_VER
    _aligned_free (p);
#else
    std::free (p);
#endif
}
void start() { bytes = 0; active = true; }
std::size_t stop() { active = false; return bytes; }
}
void* operator new (std::size_t n) { return allocation::get (n); }
void* operator new[] (std::size_t n) { return allocation::get (n); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }
void* operator new (std::size_t n, const std::nothrow_t&) noexcept { return allocation::get (n); }
void* operator new[] (std::size_t n, const std::nothrow_t&) noexcept { return allocation::get (n); }
void operator delete (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { std::free (p); }
void* operator new (std::size_t n, std::align_val_t a) { return allocation::aligned (n, std::size_t (a)); }
void* operator new[] (std::size_t n, std::align_val_t a) { return allocation::aligned (n, std::size_t (a)); }
void operator delete (void* p, std::align_val_t) noexcept { allocation::freeAligned (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { allocation::freeAligned (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { allocation::freeAligned (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { allocation::freeAligned (p); }
void* operator new (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocation::aligned (n, std::size_t (a)); }
void* operator new[] (std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return allocation::aligned (n, std::size_t (a)); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept { allocation::freeAligned (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { allocation::freeAligned (p); }

namespace
{
using namespace felitronics::toml;
struct Ratios
{
    const char* name;
    std::size_t documents = 0;
    double parse = 0, read = 0, tightParse = 0, tightRead = 0;
    void print() const
    {
        std::printf ("%s: %zu documents; actual/count parse %.4f read %.4f; declared/actual parse %.4f read %.4f\n",
                     name, documents, parse, read, tightParse, tightRead);
    }
};
Ratios realistic { "realistic" }, adversarial { "adversarial" }, invalid { "invalid" };
std::array<bool, 32> errors {};

void fields (Reader& in, int mode)
{
    if (mode == 0) return;
    for (const auto& e : in.data().entries())
    {
        if (std::holds_alternative<Table> (e.value.data))
            (void) in.table (e.key, Need::Required, [mode] (Reader& sub) { fields (sub, mode); });
        else if (std::holds_alternative<Tables> (e.value.data))
            (void) in.tables (e.key, Need::Required, [mode] (Reader& sub) { fields (sub, mode); });
        else if (mode == 2) in.refuse (e.key);
        else if (mode == 3)
        {
            if (std::holds_alternative<Array> (e.value.data))
            { std::vector<std::int64_t> v; (void) in.required (e.key, v, { 0, 0 }); }
            else { std::int64_t v = 0; (void) in.required (e.key, v, { 0, 0 }); }
        }
        else if (mode == 4)
        {
            if (std::holds_alternative<bool> (e.value.data))
            { std::string v; (void) in.required (e.key, v); }
            else { bool v = false; (void) in.required (e.key, v); }
        }
        else if (mode == 5) continue;
        else if (const auto* a = std::get_if<Array> (&e.value.data))
        {
            if (a->empty() || std::holds_alternative<std::string> (a->front().data))
            { std::vector<std::string> v; (void) in.required (e.key, v); }
            else if (std::holds_alternative<bool> (a->front().data))
            { std::vector<bool> v; (void) in.required (e.key, v); }
            else if (std::holds_alternative<Decimal> (a->front().data))
            { std::vector<Decimal> v; (void) in.required (e.key, v); }
            else { std::vector<std::int64_t> v; (void) in.required (e.key, v); }
        }
        else if (std::holds_alternative<std::string> (e.value.data))
        { std::string v; (void) in.required (e.key, v); }
        else if (std::holds_alternative<bool> (e.value.data))
        { bool v = false; (void) in.required (e.key, v); }
        else if (std::holds_alternative<Decimal> (e.value.data))
        { Decimal v; (void) in.required (e.key, v); }
        else { std::int64_t v = 0; (void) in.required (e.key, v); }
        if (mode == 6 || (mode == 2 && (std::holds_alternative<Table> (e.value.data) || std::holds_alternative<Tables> (e.value.data))))
            in.refuse (e.key);
    }
}
void check (const std::string& text, const std::string& name, Ratios& group, bool tight = false, bool evidence = false)
{
    allocation::start();
    const auto declared = storageFor (text, ReadStorage{});
    const auto again = storageFor (text);
    const auto repeated = storageFor (text, ReadStorage{});
    const auto countAllocations = allocation::stop();
    test::ok (countAllocations == 0, name + ": allocation-free count");
    test::ok (again == declared.parse && repeated.parse == declared.parse && repeated.read == declared.read, name + ": deterministic count");
    allocation::start();
    auto parsed = parse (text);
    const auto parseBytes = allocation::stop();
    if (evidence) std::printf ("REGRESSION %s: parse declared %zu requested %zu\n", name.c_str(), declared.parse, parseBytes);
    if (declared.parse < parseBytes)
        std::printf ("UNDER parse %s: %zu < %zu\n", name.c_str(), declared.parse, parseBytes);
    test::ok (declared.parse >= parseBytes, name + ": parse bound");
    ++group.documents;
    if (declared.parse != 0) group.parse = std::max (group.parse, 2.0 * double (parseBytes) / double (declared.parse));
    if (parseBytes != 0 && tight)
    {
        group.tightParse = std::max (group.tightParse, double (declared.parse) / double (parseBytes));
        test::ok (double (declared.parse) <= 8 * double (parseBytes), name + ": parse tightness <= 8");
    }
    if (const auto* error = std::get_if<Error> (&parsed)) { errors[std::size_t (error->code)] = true; return; }
    const auto& root = std::get<Table> (parsed);
    const auto canonical = writeChecked (root);
    allocation::start();
    const auto countedText = detail::Writer<detail::CountText>{}.run (root);
    const auto writerAllocations = allocation::stop();
    test::ok (writerAllocations == 0 && countedText && canonical && countedText->size() == canonical->size(),
              name + ": size-only writer matches canonical bytes without allocating");
    std::size_t worstRead = 0;
    for (int mode = 0; mode < 7; ++mode)
    {
        allocation::start();
        const auto report = read (root, [mode] (Reader& in) { fields (in, mode); });
        const auto readBytes = allocation::stop();
        if (evidence && mode == 3)
            std::printf ("REGRESSION %s: range read declared %zu requested %zu\n", name.c_str(), declared.read, readBytes);
        worstRead = std::max (worstRead, readBytes);
        if (declared.read < readBytes)
            std::printf ("UNDER read %s mode %d: %zu < %zu\n", name.c_str(), mode, declared.read, readBytes);
        test::ok (declared.read >= readBytes, name + ": read bound");
        if (mode == 1) test::ok (report.ok(), name + ": typed read succeeds");
    }
    if (declared.read != 0) group.read = std::max (group.read, 2.0 * double (worstRead) / double (declared.read));
    if (worstRead != 0 && tight)
    {
        group.tightRead = std::max (group.tightRead, double (declared.read) / double (worstRead));
        test::ok (double (declared.read) <= 8 * double (worstRead), name + ": read tightness <= 8");
    }
}

void regressions()
{
    std::string strings = "a=[";
    for (int i = 0; i < 17; ++i)
    {
        if (i != 0) strings += ',';
        strings += '"'; strings += std::string (45658, 'x'); strings += '"';
    }
    strings += ']';
    check (strings, "17 large strings", adversarial, false, true);

    // Every basic-string escape, both Unicode escape widths, UTF-8 widths and every writer control spelling.
    for (const std::string_view escape : { "\\b", "\\t", "\\n", "\\f", "\\r", "\\\"", "\\\\",
                                          "\\u0000", "\\u001F", "\\u007F", "\\u0123", "\\u1234", "\\U0001F408" })
        for (const std::size_t n : { 1u, 15u, 16u, 22u, 23u, 31u, 32u, 63u, 64u, 127u, 128u, 255u, 256u })
        {
            const std::size_t width = escape == "\\u0123" ? 2u : escape == "\\u1234" ? 3u : escape == "\\U0001F408" ? 4u : 1u;
            if (n * width > kMaxKey) continue;
            std::string key = "\"";
            for (std::size_t i = 0; i < n; ++i) key += escape;
            key += '"';
            const auto name = std::string (escape) + " path x" + fixtures::decimalInteger (n);
            check (key + "=[1]", name, adversarial, false, escape == "\\u0000" && n == 128);
            check (key + "." + key + "=[1]", "nested " + name, adversarial);
            check ("[[" + key + "]]\n" + key + "=[1]", "table array " + name, adversarial);
            const auto parsed = parse (key + "=[1]");
            test::ok (std::holds_alternative<Table> (parsed), name + ": escaped key is valid");
            if (const auto* root = std::get_if<Table> (&parsed))
            {
                const auto& decoded = root->entries().front().key;
                std::string expected;
                detail::keyText (expected, decoded);
                expected += "[0]";
                const auto report = read (*root, [&] (Reader& in)
                { std::vector<std::int64_t> values; (void) in.required (decoded, values, { 0, 0 }); });
                test::ok (report.problems.size() == 1 && report.problems.front().path == expected,
                          name + ": indexed problem preserves the escaped path");
            }
        }
    // Expanded report paths exceed MSVC's large-allocation threshold despite short decoded keys.
    std::string key = "\"", path;
    for (std::size_t i = 0; i < kMaxKey; ++i) key += "\\u0000";
    key += '"';
    for (std::size_t depth = 0; depth < kMaxDepth; ++depth)
    {
        if (! path.empty()) path += '.';
        path += key;
        check (path + "=[1]", "deep escaped range path", adversarial);
        if (depth + 1 < kMaxDepth) check ("[[" + path + "]]\na=[1]", "deep escaped table-array path", adversarial);
    }
}
}

int main (int argc, char** argv)
{
    using namespace felitronics::toml;
    static_assert (noexcept (storageFor (std::string_view{})) && noexcept (storageFor (std::string_view{}, ReadStorage{})));
    if (argc != 2 && argc != 3) return 2;
#if defined(_MSC_VER) && defined(_DEBUG)
    static_assert (_ITERATOR_DEBUG_LEVEL == 2);
#endif
    std::printf ("pointer=%zu Entry=%zu Value=%zu Table=%zu Problem=%zu proxy=%zu\n",
                 sizeof (void*), sizeof (Entry), sizeof (Value), sizeof (Table), sizeof (Problem), detail::StorageCount::proxyBytes);
    regressions();
    if (argc == 3 && std::string_view (argv[2]) == "--regressions") return test::report();
    for (const auto& f : std::filesystem::recursive_directory_iterator (argv[1]))
    {
        if (f.path().extension() != ".toml") continue;
        std::ifstream stream (f.path(), std::ios::binary);
        const std::string text { std::istreambuf_iterator<char> (stream), std::istreambuf_iterator<char>() };
        const bool bad = f.path().parent_path().filename() == "invalid";
        const auto name = f.path().filename().string();
        const bool stress = text.size() >= 65536 || name.starts_with ("limit-") || name.starts_with ("generated-depth-");
        check (text, name, bad ? invalid : stress ? adversarial : realistic, ! bad && ! stress);
    }
    fixtures::Generator g { 0x74736f72 };
    for (int i = 0; i < 64; ++i) check (write (fixtures::generated (g)), "generated", realistic, true);
    for (std::size_t n : { 0u, 1u, 15u, 16u, 17u, 22u, 23u, 31u, 32u, 33u, 63u, 64u, 65u, 127u, 128u, 129u,
                           255u, 256u, 257u, 511u, 512u, 513u, 1023u, 1024u, 1025u, 2047u, 2048u, 2049u,
                           4095u, 4096u, 4097u, 32769u, 65536u })
    {
        std::string keys, headers, tables, array = "a=[", inlines = "a=[", strings = "a=[";
        for (std::size_t i = 0; i < n; ++i)
        {
            const auto key = fixtures::decimalInteger (i);
            keys += "k" + key + "=0\n";
            headers += "[t" + key + "]\na=0\n";
            tables += "[[a]]\nb=0\n";
            array += i == 0 ? "0" : ",0";
            strings += i == 0 ? "\"x\"" : ",\"x\"";
            inlines += i == 0 ? "{b=0}" : ",{b=0}";
        }
        array += ']'; inlines += ']'; strings += ']';
        check (keys, "tiny keys", adversarial);
        check (headers, "headers", adversarial);
        check (tables, "table arrays", adversarial);
        check (array, "array", adversarial);
        check (strings, "string array", adversarial);
        check (inlines, "inline tables", adversarial);
        check ("a=\"" + std::string (n, 'x') + "\"", "string growth", adversarial);
    }
    std::string capacityProbe;
    while (capacityProbe.size() <= kMaxString)
    {
        const auto n = capacityProbe.capacity() + 1;
        if (n > kMaxString) break;
        capacityProbe.resize (n, 'x');
        check ("a=\"" + capacityProbe + "\"", "native string growth boundary", adversarial);
        std::string repeated = "a=[";
        const auto copies = std::min (std::size_t (17), (kMaxDocument - 4) / (n + 3));
        for (std::size_t i = 0; i < copies; ++i)
        {
            if (i != 0) repeated += ',';
            repeated += '"'; repeated += capacityProbe; repeated += '"';
        }
        repeated += ']';
        check (repeated, "repeated native string growth boundary", adversarial);
    }
    std::string path;
    for (std::size_t depth = 0; depth <= kMaxDepth; ++depth)
    {
        if (! path.empty()) path += '.';
        path += std::string (kMaxKey, 'x');
        check (path + "=1", "deep long path", adversarial);
        check ("[[" + path + "]]\na=1", "deep table array", adversarial);
    }
    for (const std::string sample : { "a={b=[{x=1},{x=2}]}\n", "[[a.b]]\nc=\"\\u0123\"\n", "a=1_234.567_89\n" })
        for (std::size_t n = 0; n <= sample.size(); ++n) check (sample.substr (0, n), "truncated syntax", invalid);
    for (int i = 0; i < 2000; ++i)
    {
        std::string text;
        for (std::size_t j = 0, n = g.next() % 200; j < n; ++j) text += char (g.next() & 255);
        check (text, "arbitrary bytes", invalid);
    }
    for (std::size_t n = 0; n <= kMaxKey; ++n)
        check ("\"" + std::string (n, 'k') + "\"=[1]", "indexed problem path growth", adversarial);
    std::string lateRange = "array_with_a_long_name=[";
    for (std::size_t i = 0; i < 4096; ++i) lateRange += "0,";
    lateRange += "1]";
    check (lateRange, "late array range failure", adversarial);
    const std::string missing (1024, 'm');
    const std::vector<std::string_view> extras (4097, missing);
    allocation::start();
    const auto budget = storageFor ("", ReadStorage { extras });
    const auto extraAllocations = allocation::stop();
    test::ok (extraAllocations == 0, "extra path count allocates nothing");
    test::ok (detail::storageAdd (std::numeric_limits<std::size_t>::max(), 1) == std::numeric_limits<std::size_t>::max(), "saturating sum");
    test::ok (detail::storageMultiply (std::numeric_limits<std::size_t>::max(), 2) == std::numeric_limits<std::size_t>::max(), "saturating product");
    const auto empty = parse ("");
    allocation::start();
    const auto report = read (std::get<Table> (empty), [&] (Reader& in)
    {
        for (const auto key : extras) (void) in.field (key, Need::Required);
    });
    const auto actual = allocation::stop();
    test::ok (budget.read >= actual && report.problems.size() == extras.size(), "many missing required keys");
    adversarial.read = std::max (adversarial.read, 2.0 * double (actual) / double (budget.read));
    allocation::start();
    const auto refused = read (std::get<Table> (empty), [&] (Reader& in)
    {
        for (const auto key : extras) in.refuse (key);
    });
    const auto refusedBytes = allocation::stop();
    test::ok (budget.read >= refusedBytes && refused.problems.size() == extras.size(), "many custom refusals");
    for (std::size_t i = 0; i < errors.size(); ++i) test::ok (errors[i], std::string (codeName (Code (i))) + " exercised");
    realistic.print(); adversarial.print(); invalid.print();
    return test::report();
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Oleh Lafoks and Alisa Lafoks. Part of felitronics-toml, see LICENSE.
#pragma once

// The whole test harness: a check counter, a group heading and the exit code. Each suite owns its main()
// and ends with `return test::report();`. Its own namespace, so it cannot collide with another harness
// in a build that compiles these suites next to someone else's.
#include <cstdio>
#include <string>

namespace felitronics::toml::test
{
struct Stats { long checks = 0, failures = 0; };
inline Stats& stats() { static Stats s; return s; }

inline void ok (bool cond, const std::string& msg)
{
    ++stats().checks;
    if (! cond) { ++stats().failures; std::printf ("    FAIL: %s\n", msg.c_str()); }
}

inline void group (const std::string& name) { std::printf ("  - %s\n", name.c_str()); }

inline int report()
{
    const auto& s = stats();
    std::printf ("\n%ld checks, %ld failures\n%s\n", s.checks, s.failures,
                 s.failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED");
    return s.failures == 0 ? 0 : 1;
}
}

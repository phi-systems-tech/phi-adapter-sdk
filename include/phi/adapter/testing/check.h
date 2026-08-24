#pragma once

// The assertion helpers the phi test suites share.
//
// Adapters had no tests at all, which is how three copies of one enum table
// came to live side by side in three of them (finding U-28). They have very
// little in common with each other and nothing that needs a framework: what is
// worth testing in an adapter is conversion, with no device and no network
// behind it - a vendor payload in, a v1 type out - and that needs an assertion
// that says where it failed and a count at the end.
//
// This is that, in the form phi-core has used since its first test, so a
// suite reads the same wherever it lives. One test binary per subject, wired
// into ctest, run by `cmake --build . && ctest`.

#include <cstdio>

namespace phi::testing {

inline int g_failures = 0;

#define PHI_CHECK(cond)                                                                         \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                         \
            ++::phi::testing::g_failures;                                                       \
        }                                                                                       \
    } while (false)

#define PHI_CHECK_MSG(cond, ...)                                                                \
    do {                                                                                        \
        if (!(cond)) {                                                                          \
            std::printf("FAIL %s:%d: %s (", __FILE__, __LINE__, #cond);                         \
            std::printf(__VA_ARGS__);                                                           \
            std::printf(")\n");                                                                 \
            ++::phi::testing::g_failures;                                                       \
        }                                                                                       \
    } while (false)

/// Prints the verdict and answers with the process exit code.
inline int report(const char *name)
{
    if (g_failures == 0) {
        std::printf("%s: all passed\n", name);
        return 0;
    }
    std::printf("%s: %d failure(s)\n", name, g_failures);
    return 1;
}

} // namespace phi::testing

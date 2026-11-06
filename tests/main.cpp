// Runs every registered test and prints ALL TESTS PASSED (exit 0) or the failures (exit 1).
#include "check.h"

TEST(harness_runs)
{
    CHECK_EQ(1 + 1, 2);
}

int main()
{
    int failed = 0;
    for (auto& t : check::registry()) {
        check::currentFailed() = false;
        t.fn();
        if (check::currentFailed()) { std::printf("FAILED %s\n", t.name); ++failed; }
    }
    if (failed) { std::printf("%d of %zu tests FAILED\n", failed, check::registry().size()); return 1; }
    std::printf("ALL TESTS PASSED (%zu)\n", check::registry().size());
    return 0;
}

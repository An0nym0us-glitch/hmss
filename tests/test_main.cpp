#include "TestFramework.h"

#include <chrono>
#include <cstring>

int main(int argc, char** argv)
{
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failedTests = 0;
    for (const auto& t : abtest::registry()) {
        if (filter && !std::strstr(t.name, filter))
            continue;
        const int before = abtest::failures();
        std::printf("[ RUN  ] %s\n", t.name);
        std::fflush(stdout);
        const auto t0 = std::chrono::steady_clock::now();
        t.fn();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        const bool ok = abtest::failures() == before;
        std::printf("[ %s ] %s (%.0f ms)\n", ok ? " OK " : "FAIL", t.name, ms);
        ++run;
        if (!ok)
            ++failedTests;
    }
    std::printf("\n%d test(s) run, %d failed\n", run, failedTests);
    return failedTests == 0 && run > 0 ? 0 : 1;
}

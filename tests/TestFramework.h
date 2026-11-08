// Minimal dependency-free test framework for AudioBridge.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace abtest {

struct TestCase {
    const char* name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& registry()
{
    static std::vector<TestCase> r;
    return r;
}

inline int& failures()
{
    static int f = 0;
    return f;
}

struct Registrar {
    Registrar(const char* n, std::function<void()> f) { registry().push_back({n, std::move(f)}); }
};

inline void report(bool ok, const char* expr, const char* file, int line, const std::string& msg)
{
    if (!ok) {
        ++failures();
        std::printf("    FAILED %s:%d: %s %s\n", file, line, expr, msg.c_str());
    }
}

} // namespace abtest

#define AB_CONCAT2(a, b) a##b
#define AB_CONCAT(a, b) AB_CONCAT2(a, b)
#define TEST(name)                                                                          \
    static void name();                                                                     \
    static abtest::Registrar AB_CONCAT(reg_, name)(#name, name);                            \
    static void name()

#define CHECK(cond) abtest::report((cond), #cond, __FILE__, __LINE__, "")
#define CHECK_MSG(cond, msg)                                                                \
    do {                                                                                    \
        std::ostringstream os_;                                                             \
        os_ << msg;                                                                         \
        abtest::report((cond), #cond, __FILE__, __LINE__, os_.str());                       \
    } while (0)
#define CHECK_NEAR(a, b, tol)                                                               \
    do {                                                                                    \
        const double va_ = (a), vb_ = (b);                                                  \
        std::ostringstream os_;                                                             \
        os_ << "(" << va_ << " vs " << vb_ << ", tol " << (tol) << ")";                     \
        abtest::report(std::fabs(va_ - vb_) <= (tol), #a " ~= " #b, __FILE__, __LINE__, os_.str()); \
    } while (0)

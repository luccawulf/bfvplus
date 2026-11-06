// A minimal test harness for bfvplus's platform-independent modules. Tests build with
// g++ in WSL (tests/run.sh); the same modules build with MSVC into dinput8.dll.
#pragma once
#include <cstdio>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

namespace check {

struct Test { const char* name; std::function<void()> fn; };
inline std::vector<Test>& registry() { static std::vector<Test> r; return r; }
inline int& failures() { static int n = 0; return n; }
inline bool& currentFailed() { static bool b = false; return b; }

struct Registrar {
    Registrar(const char* name, std::function<void()> fn) { registry().push_back({ name, std::move(fn) }); }
};

template <class T> std::string show(const T& v) { std::ostringstream o; o << v; return o.str(); }
inline std::string show(const std::string& v) { return "\"" + v + "\""; }
inline std::string show(const char* v) { return "\"" + std::string(v) + "\""; }
inline std::string show(const std::vector<std::string>& v)
{
    std::string s = "[";
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + show(v[i]);
    return s + "]";
}

inline void fail(const char* file, int line, const std::string& what)
{
    std::printf("  FAIL %s:%d: %s\n", file, line, what.c_str());
    currentFailed() = true;
}

}  // namespace check

#define CHECK_CAT2(a, b) a##b
#define CHECK_CAT(a, b) CHECK_CAT2(a, b)
#define TEST(name)                                                                  \
    static void name();                                                             \
    static check::Registrar CHECK_CAT(reg_, name)(#name, name);                     \
    static void name()

#define CHECK(cond) \
    do { if (!(cond)) check::fail(__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_EQ(a, b)                                                              \
    do {                                                                            \
        auto&& check_a_ = (a); auto&& check_b_ = (b);                               \
        if (!(check_a_ == check_b_))                                                \
            check::fail(__FILE__, __LINE__, std::string(#a " == " #b "\n    got:  ") \
                + check::show(check_a_) + "\n    want: " + check::show(check_b_));  \
    } while (0)

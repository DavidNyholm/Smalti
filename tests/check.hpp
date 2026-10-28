// A tiny test harness so the suite needs no third-party framework (adapted from etch's).
//
//   TEST(name) { CHECK(cond); CHECK_EQ(a, b); CHECK_NEAR(a, b, tolerance); }
//
// Run with a substring argument to select tests: smalti_tests layout
#pragma once

#include <chrono>
#include <cmath>
#include <cstdio>
#include <exception>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace check {

struct Test {
  const char* name;
  void (*fn)();
};

inline std::vector<Test>& registry() {
  static std::vector<Test> tests;
  return tests;
}

inline int& current_failures() {
  static int failures = 0;
  return failures;
}

inline void fail(std::string_view what, const char* file, int line) {
  ++current_failures();
  std::printf("    FAILED %s:%d: %.*s\n", file, line, static_cast<int>(what.size()), what.data());
}

inline void require(bool ok, const char* expr, const char* file, int line) {
  if (!ok) fail(expr, file, line);
}

template <class T>
concept Streamable = requires(std::ostream& os, const T& v) { os << v; };

template <class A, class B>
void require_eq(const A& a, const B& b, const char* ea, const char* eb, const char* file, int line) {
  if (a == b) return;
  std::ostringstream msg;
  msg << ea << " == " << eb;
  if constexpr (Streamable<A> && Streamable<B>) msg << "  (got [" << a << "] vs [" << b << "])";
  fail(msg.str(), file, line);
}

inline void require_near(double a, double b, double tol, const char* ea, const char* eb, const char* file, int line) {
  if (std::fabs(a - b) <= tol) return;
  std::ostringstream msg;
  msg << ea << " ~= " << eb << "  (got " << a << " vs " << b << ", tolerance " << tol << ")";
  fail(msg.str(), file, line);
}

inline int run(int argc, char** argv) {
  const std::string_view filter = argc > 1 ? argv[1] : "";
  int passed = 0, failed = 0;
  const auto start = std::chrono::steady_clock::now();
  for (const auto& test : registry()) {
    if (!filter.empty() && std::string_view(test.name).find(filter) == std::string_view::npos) continue;
    const int before = current_failures();
    try {
      test.fn();
    } catch (const std::exception& e) {
      fail(std::string("unexpected exception: ") + e.what(), "?", 0);
    } catch (...) {
      fail("unexpected non-std exception", "?", 0);
    }
    const bool ok = current_failures() == before;
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", test.name);
    (ok ? passed : failed)++;
  }
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
  std::printf("\n%d passed, %d failed, %d total, %lld ms\n", passed, failed, passed + failed,
              static_cast<long long>(ms.count()));
  return failed == 0 ? 0 : 1;
}

}  // namespace check

#define TEST(name)                                                                           \
  static void name();                                                                        \
  static const bool name##_registered = (check::registry().push_back({#name, &name}), true); \
  static void name()

#define CHECK(cond) check::require(static_cast<bool>(cond), #cond, __FILE__, __LINE__)
#define CHECK_EQ(a, b) check::require_eq((a), (b), #a, #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, tol) check::require_near((a), (b), (tol), #a, #b, __FILE__, __LINE__)

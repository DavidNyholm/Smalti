// smalti tests: the entry point, and the allocation counter. Each test_*.cpp registers its tests
// at static-initialisation time.
#include <cstdio>
#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <crtdbg.h>
#include <windows.h>
#endif

#include "check.hpp"
#include "support.hpp"

namespace alloc_count {
std::atomic<long long>& count() {
  static std::atomic<long long> n{0};
  return n;
}
std::atomic<long long>& bytes() {
  static std::atomic<long long> n{0};
  return n;
}
std::atomic<bool>& enabled() {
  static std::atomic<bool> on{false};
  return on;
}
}  // namespace alloc_count

namespace {

void* counted_malloc(std::size_t size) {
  if (alloc_count::enabled().load(std::memory_order_relaxed)) {
    alloc_count::count().fetch_add(1);
    alloc_count::bytes().fetch_add(static_cast<long long>(size));
  }
  return std::malloc(size ? size : 1);
}

}  // namespace

// Replacing the global allocation functions lets a test count what a code path allocates. Every
// form that pairs with these deletes is replaced, the nothrow one too (std::stable_sort uses it):
// under AddressSanitizer an unreplaced form comes from the sanitizer, which then reports our
// free() of its memory as a mismatch.
void* operator new(std::size_t size) {
  if (void* p = counted_malloc(size)) return p;
  throw std::bad_alloc();
}
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  return counted_malloc(size);
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }

#if defined(_MSC_VER)
namespace {

// The exit code when the C runtime or the standard library finds a broken precondition.
constexpr int kRuntimeCheckFailed = 3;

// A failed runtime or library check (an assert, std::clamp with reversed bounds) ends the run at
// once with a message, instead of waiting in a dialog nobody sees: the injected-bug script runs
// the suite unattended, and a dialog turns a crash into a time-out.
void fail_fast_on_runtime_checks() {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#if defined(_DEBUG)  // the report functions exist only in the debug runtime
  for (const int type : {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT}) {
    _CrtSetReportMode(type, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(type, _CRTDBG_FILE_STDERR);
  }
#endif
  _set_invalid_parameter_handler([](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int,
                                    uintptr_t) {
    std::fputs("smalti tests: the C runtime rejected an argument (an invalid parameter)\n", stderr);
    std::_Exit(kRuntimeCheckFailed);
  });
}

}  // namespace
#endif

int main(int argc, char** argv) {
#if defined(_MSC_VER)
  fail_fast_on_runtime_checks();
#endif
  std::printf("smalti tests\n");
  return check::run(argc, argv);
}

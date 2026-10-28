// smalti tests: the entry point, and the allocation counter. Each test_*.cpp registers its tests
// at static-initialisation time.
#include <cstdio>
#include <cstdlib>
#include <new>

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

// Replacing the global allocation functions lets a test count what a code path allocates.
void* operator new(std::size_t size) {
  if (alloc_count::enabled().load(std::memory_order_relaxed)) {
    alloc_count::count().fetch_add(1);
    alloc_count::bytes().fetch_add(static_cast<long long>(size));
  }
  if (void* p = std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }

int main(int argc, char** argv) {
  std::printf("smalti tests\n");
  return check::run(argc, argv);
}

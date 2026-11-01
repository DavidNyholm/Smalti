// Shared test helpers: a headless app, a contract handler that throws instead of aborting, and
// an allocation counter.
#pragma once

#include <atomic>
#include <cstddef>
#include <stdexcept>

#include "smalti/smalti.hpp"
#include "smalti/testing.hpp"

struct ContractError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// While alive, contract violations throw ContractError, so a test can check that one happens.
class ExpectContracts {
 public:
  ExpectContracts() {
    smalti::set_contract_handler([](const char* m) { throw ContractError(m); });
  }
  ~ExpectContracts() { smalti::set_contract_handler(nullptr); }
  ExpectContracts(const ExpectContracts&) = delete;
  ExpectContracts& operator=(const ExpectContracts&) = delete;
};

inline smalti::AppOptions headless(const smalti::Theme* theme = &smalti::theme_dark()) {
  smalti::AppOptions o;
  o.backend = smalti::Backend::headless;
  o.theme = theme;
  return o;
}

// Counts heap allocations, and the bytes they ask for, while enabled (operator new is replaced
// in main.cpp).
namespace alloc_count {
std::atomic<long long>& count();
std::atomic<long long>& bytes();
std::atomic<bool>& enabled();
struct Scope {
  Scope() {
    count() = 0;
    bytes() = 0;
    enabled() = true;
  }
  ~Scope() { enabled() = false; }
  long long allocations() const { return count().load(); }
  long long allocated_bytes() const { return bytes().load(); }
};
// While a FailOne lives, the allocation of at least `min_bytes` after `skip` others fails
// (operator new throws std::bad_alloc), and only that one: code can be checked for what it
// leaves behind when memory runs out at each point in turn. Smaller allocations are left
// alone: MSVC's debug containers make small bookkeeping ones inside moves declared noexcept,
// where a failure ends the program whatever the code around them does.
std::atomic<long long>& fail_countdown();  // -1: none fails
std::atomic<size_t>& fail_min_bytes();
struct FailOne {
  FailOne(long long skip, size_t min_bytes) {
    fail_min_bytes() = min_bytes;
    fail_countdown() = skip;
  }
  ~FailOne() { fail_countdown() = -1; }
  FailOne(const FailOne&) = delete;
  FailOne& operator=(const FailOne&) = delete;
  bool failed() const { return fail_countdown().load() == -1; }
};
}  // namespace alloc_count

template <class F>
bool throws_contract(F&& f) {
  try {
    f();
  } catch (const ContractError&) {
    return true;
  }
  return false;
}

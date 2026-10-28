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

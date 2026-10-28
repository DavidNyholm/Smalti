// The widget registry behind Ref, and the contract handler.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "core/state.hpp"
#include "smalti/widget.hpp"

namespace smalti {

namespace {

[[noreturn]] void print_and_abort(const char* message) {
  std::fprintf(stderr, "smalti: contract violation: %s\n", message);
  std::fflush(stderr);
  std::abort();
}

ContractHandler g_handler = &print_and_abort;

}  // namespace

void set_contract_handler(ContractHandler handler) { g_handler = handler ? handler : &print_and_abort; }

namespace detail {

[[noreturn]] void contract_violation(const char* message) {
  g_handler(message);
  // A handler must not return; if it does, stop here rather than continue in a broken state.
  print_and_abort(message);
}

[[noreturn]] void dead_ref(WidgetId id) {
  const std::string message = "a Ref was used after its widget was removed (slot " + std::to_string(id.slot) +
                              ", generation " + std::to_string(id.generation) +
                              "). Test it with `if (ref)` when the widget may be gone.";
  contract_violation(message.c_str());
}

Widget* resolve(WidgetId id) {
  const Registry& r = Registry::instance();
  r.check_thread();
  return r.find(id);
}

Registry& Registry::instance() {
  static Registry registry;
  return registry;
}

WidgetId Registry::allocate(Widget* widget) {
  if (!ui_thread_) contract_violation("widgets are created through an App: create the App first");
  check_thread();
  uint32_t slot;
  if (!free_.empty()) {
    slot = free_.back();
    free_.pop_back();
  } else {
    slot = static_cast<uint32_t>(slots_.size());
    slots_.push_back({});
  }
  Slot& s = slots_[slot];
  s.widget = widget;
  ++s.generation;  // never wraps: release() retires a slot before it could
  ++live_;
  return {slot, s.generation};
}

void Registry::release(WidgetId id) {
  if (id.slot == 0 || id.slot >= slots_.size()) return;
  Slot& s = slots_[id.slot];
  if (s.generation != id.generation || s.widget == nullptr) return;
  s.widget = nullptr;
  ++s.generation;  // every outstanding Ref to this slot is now stale
  // A slot whose generation would wrap is never reused: a Ref from four billion widgets ago
  // must not come back to life. Retiring one costs its 16 bytes.
  if (s.generation < UINT32_MAX - 1) free_.push_back(id.slot);
  --live_;
}

Widget* Registry::find(WidgetId id) const {
  if (id.slot == 0 || id.slot >= slots_.size()) return nullptr;
  const Slot& s = slots_[id.slot];
  return s.generation == id.generation ? s.widget : nullptr;
}

void Registry::check_thread() const {
  if (ui_thread_ && *ui_thread_ != std::this_thread::get_id()) {
    contract_violation("smalti was called from a thread other than the UI thread; use App::post to run code there");
  }
}

}  // namespace detail
}  // namespace smalti

// Ref<T>: the only way the API hands out widgets. A Ref is a slot index and a generation
// number; it owns nothing, is safe to copy into any callback, and can never reach freed memory.
// Using a Ref whose widget was removed, or using one from a thread other than the UI thread,
// is a contract violation: the contract handler reports it (by default it prints and aborts).
#pragma once

#include <concepts>
#include <cstdint>

namespace smalti {

class Widget;

struct WidgetId {
  uint32_t slot = 0;
  uint32_t generation = 0;  // 0 never names a live widget
  friend bool operator==(WidgetId, WidgetId) = default;
};

// Called with a description of the broken contract. The default prints it and aborts; tests
// install one that throws. The handler must not return.
using ContractHandler = void (*)(const char* message);
void set_contract_handler(ContractHandler handler);

namespace detail {
Widget* resolve(WidgetId id);  // nullptr if removed; a contract violation off the UI thread
[[noreturn]] void contract_violation(const char* message);
[[noreturn]] void dead_ref(WidgetId id);
}  // namespace detail

template <class T>
class Ref {
 public:
  Ref() = default;

  // Checked access: a contract violation if the widget is gone.
  T& get() const {
    T* p = try_get();
    if (!p) detail::dead_ref(id_);
    return *p;
  }
  T* operator->() const { return &get(); }
  T& operator*() const { return get(); }

  // The widget, or nullptr if it has been removed. Still a contract violation off the UI thread.
  T* try_get() const { return static_cast<T*>(detail::resolve(id_)); }
  explicit operator bool() const { return try_get() != nullptr; }

  WidgetId id() const { return id_; }

  // Ref<Button> converts to Ref<Widget>, never the other way implicitly.
  template <class U>
    requires std::derived_from<T, U>
  operator Ref<U>() const {
    return Ref<U>::from_id(id_);
  }

  // For the library's own use: the caller guarantees the slot holds a T.
  static Ref from_id(WidgetId id) {
    Ref r;
    r.id_ = id;
    return r;
  }

  friend bool operator==(const Ref&, const Ref&) = default;

 private:
  WidgetId id_;
};

// A checked downcast: an empty Ref if `r` is gone or is not a T.
template <class T, class U>
Ref<T> ref_cast(Ref<U> r) {
  return dynamic_cast<T*>(r.try_get()) ? Ref<T>::from_id(r.id()) : Ref<T>{};
}

}  // namespace smalti

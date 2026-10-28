// Callback<R(Args...)>: a move-only function, so a callback can own what it captures (a
// unique_ptr, a file). std::move_only_function would do, but it is C++23.
#pragma once

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

namespace smalti {

template <class Signature>
class Callback;

template <class R, class... Args>
class Callback<R(Args...)> {
 public:
  Callback() = default;
  template <class F>
    requires(!std::is_same_v<std::decay_t<F>, Callback> && std::is_invocable_r_v<R, F&, Args...>)
  Callback(F&& f) : impl_(std::make_shared<Impl<std::decay_t<F>>>(std::forward<F>(f))) {}
  Callback(Callback&&) noexcept = default;
  Callback& operator=(Callback&&) noexcept = default;
  Callback(const Callback&) = delete;
  Callback& operator=(const Callback&) = delete;

  explicit operator bool() const { return impl_ != nullptr; }

  // The running callable is kept alive until it returns, so a callback may replace or clear
  // the Callback that is calling it (a click handler that installs a new click handler).
  R operator()(Args... args) const {
    const std::shared_ptr<Base> running = impl_;
    return running->call(std::forward<Args>(args)...);
  }

 private:
  struct Base {
    virtual ~Base() = default;
    virtual R call(Args... args) = 0;
  };
  template <class F>
  struct Impl final : Base {
    explicit Impl(F f) : fn(std::move(f)) {}
    R call(Args... args) override {
      if constexpr (std::is_void_v<R>) {
        std::invoke(fn, std::forward<Args>(args)...);  // a result the signature has no room for
      } else {
        return std::invoke(fn, std::forward<Args>(args)...);
      }
    }
    F fn;
  };
  // shared, not unique, only so operator() can hold the callable while it runs; a Callback
  // itself cannot be copied.
  std::shared_ptr<Base> impl_;
};

}  // namespace smalti

// Callback<R(Args...)>: a move-only function, so a callback can own what it captures (a
// unique_ptr, a file). std::move_only_function would do, but it is C++23.
#pragma once

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
  Callback(F&& f) : impl_(std::make_unique<Impl<std::decay_t<F>>>(std::forward<F>(f))) {}

  explicit operator bool() const { return impl_ != nullptr; }
  R operator()(Args... args) const { return impl_->call(std::forward<Args>(args)...); }

 private:
  struct Base {
    virtual ~Base() = default;
    virtual R call(Args... args) = 0;
  };
  template <class F>
  struct Impl final : Base {
    explicit Impl(F f) : fn(std::move(f)) {}
    R call(Args... args) override { return fn(std::forward<Args>(args)...); }
    F fn;
  };
  std::unique_ptr<Base> impl_;
};

}  // namespace smalti

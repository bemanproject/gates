// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_SERIALIZED_ACCESS_HPP
#define BEMAN_GATES_SERIALIZED_ACCESS_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)
import beman.gates;
import beman.execution;
#else
    #include <beman/gates/serial_gate.hpp>

    #include <beman/execution/execution.hpp>
#endif

#ifdef BEMAN_HAS_IMPORT_STD
import std;
#else
    #include <concepts>
    #include <functional>
    #include <type_traits>
    #include <utility>
#endif

namespace beman::gates::detail {

/// Sender adaptor used by serialized_access's access functions.
template <bool Async, bool Try, typename Access, typename F>
struct serialized_access_closure
    : ::beman::execution::sender_adaptor_closure<serialized_access_closure<Async, Try, Access, F>> {
    Access* access;
    F       fun;

    template <typename G>
    serialized_access_closure(Access* access_, G&& fun_) : access(access_), fun(::std::forward<G>(fun_)) {}

    template <typename Self, ::beman::execution::sender Sender>
    auto operator()(this Self&& self, Sender&& sender) {
        auto* access = self.access;
        auto  invoke = [access, fun = ::std::forward<Self>(self).fun](auto&&... args) mutable -> decltype(auto) {
            return ::std::invoke(::std::move(fun), access->value_, ::std::forward<decltype(args)>(args)...);
        };

        auto scope = [&] {
            if constexpr (Try) {
                return access->gate_.try_acquire();
            } else {
                return access->gate_.acquire();
            }
        }();

        if constexpr (Async) {
            return ::beman::execution::within(
                std::move(scope), ::std::forward<Sender>(sender) | ::beman::execution::let_value(std::move(invoke)));
        } else {
            return ::beman::execution::within(
                std::move(scope), ::std::forward<Sender>(sender) | ::beman::execution::then(std::move(invoke)));
        }
    }
};

template <bool Async, bool Try, typename Access, typename F>
auto make_serialized_access_closure(Access* access, F&& fun)
    -> serialized_access_closure<Async, Try, Access, ::std::remove_cvref_t<F>> {
    return {access, ::std::forward<F>(fun)};
}

} // namespace beman::gates::detail

namespace beman::gates {

/// Associates a value with a serial gate and provides serialized access to that value.
template <typename T>
struct serialized_access {
    /// Constructs the contained `T` object with `std::forward<Args>(args)...`.
    template <typename... Args>
        requires ::std::constructible_from<T, Args...>
    explicit serialized_access(::std::in_place_t, Args&&... args) : gate_{}, value_(::std::forward<Args>(args)...) {}

    serialized_access(const serialized_access&)            = delete;
    serialized_access& operator=(const serialized_access&) = delete;

    /// Returns a sender adaptor closure that invokes `f` with the protected value followed by the predecessor's values
    /// while exclusive access is held.
    template <typename Self, typename F>
        requires ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> && ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto apply(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<false, false>(&self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that invokes `f` with the protected value followed by the predecessor's values
    /// and holds exclusive access until the sender returned by `f` completes.
    template <typename Self, typename F>
        requires ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> && ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto apply_async(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<true, false>(&self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that tries to invoke `f` as for `apply`, completing with `busy_error` without
    /// invoking `f` if exclusive access cannot be acquired immediately.
    template <typename Self, typename F>
        requires ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> && ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto try_apply(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<false, true>(&self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that tries to invoke `f` as for `apply_async`, completing with `busy_error`
    /// without invoking `f` if exclusive access cannot be acquired immediately.
    template <typename Self, typename F>
        requires ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> && ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto try_apply_async(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<true, true>(&self, ::std::forward<F>(f));
    }

  private:
    /// Gate that ensures serialized access to `value_`.
    mutable serial_gate gate_;
    /// The value protected by `gate_`.
    T value_;

    template <bool, bool, typename, typename>
    friend struct detail::serialized_access_closure;
};

} // namespace beman::gates

#endif

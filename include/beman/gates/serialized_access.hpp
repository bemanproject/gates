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

/// The way we are accessing the value stored in `serialized_access`.
enum class serialized_access_mode { exclusive, try_exclusive, shared, try_shared };

/// Concept that indicates whether `Gate` supports shared access.
template <typename Gate>
concept is_shared_gate = requires(Gate& gate) {
    { gate.acquire_shared() } -> ::beman::execution::enter_scope_sender;
    { gate.try_acquire_shared() } -> ::beman::execution::enter_scope_sender;
};

/// Sender adaptor used by serialized_access's access functions.
template <serialized_access_mode Mode, bool Async, typename Storage, typename F>
struct serialized_access_closure
    : ::beman::execution::sender_adaptor_closure<serialized_access_closure<Mode, Async, Storage, F>> {
    Storage* storage_;
    F        fun_;

    template <typename G>
    serialized_access_closure(Storage* storage, G&& f) : storage_(storage), fun_(::std::forward<G>(f)) {}

    template <typename Self, ::beman::execution::sender Sender>
    auto operator()(this Self&& self, Sender&& sender) {
        auto* storage = self.storage_;
        auto  invoke  = [storage, fun = ::std::forward<Self>(self).fun_](auto&&... args) mutable -> decltype(auto) {
            if constexpr (Mode == serialized_access_mode::shared || Mode == serialized_access_mode::try_shared) {
                return ::std::invoke(
                    ::std::move(fun), ::std::as_const(storage->value_), ::std::forward<decltype(args)>(args)...);
            } else {
                return ::std::invoke(::std::move(fun), storage->value_, ::std::forward<decltype(args)>(args)...);
            }
        };

        auto scope = [&] {
            if constexpr (Mode == serialized_access_mode::exclusive) {
                return storage->gate_.acquire();
            } else if constexpr (Mode == serialized_access_mode::try_exclusive) {
                return storage->gate_.try_acquire();
            } else if constexpr (Mode == serialized_access_mode::shared) {
                return storage->gate_.acquire_shared();
            } else if constexpr (Mode == serialized_access_mode::try_shared) {
                return storage->gate_.try_acquire_shared();
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

template <serialized_access_mode Mode, bool Async, typename Storage, typename F>
auto make_serialized_access_closure(Storage* access, F&& fun)
    -> serialized_access_closure<Mode, Async, Storage, ::std::remove_cvref_t<F>> {
    return {access, ::std::forward<F>(fun)};
}

} // namespace beman::gates::detail

namespace beman::gates {

/// Associates a value with a gate and provides exclusive access, and optionally shared access, to that value.
template <typename T, typename Gate = serial_gate>
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
        return detail::make_serialized_access_closure<detail::serialized_access_mode::exclusive, false>(
            &self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that invokes `f` with the protected value followed by the predecessor's values
    /// and holds exclusive access until the sender returned by `f` completes.
    template <typename Self, typename F>
        requires ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> && ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto apply_async(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<detail::serialized_access_mode::exclusive, true>(
            &self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that tries to invoke `f` as for `apply`, completing with `busy_error` without
    /// invoking `f` if exclusive access cannot be acquired immediately.
    template <typename Self, typename F>
        requires ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> && ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto try_apply(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<detail::serialized_access_mode::try_exclusive, false>(
            &self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that tries to invoke `f` as for `apply_async`, completing with `busy_error`
    /// without invoking `f` if exclusive access cannot be acquired immediately.
    template <typename Self, typename F>
        requires ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> && ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto try_apply_async(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<detail::serialized_access_mode::try_exclusive, true>(
            &self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that invokes `f` with the protected value as `const T&` followed by the
    /// predecessor's values while shared access is held.
    template <typename Self, typename F>
        requires detail::is_shared_gate<Gate> && ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> &&
                 ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto apply_shared(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<detail::serialized_access_mode::shared, false>(
            &self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that invokes `f` with the protected value as `const T&` followed by the
    /// predecessor's values and holds shared access until the sender returned by `f` completes.
    template <typename Self, typename F>
        requires detail::is_shared_gate<Gate> && ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> &&
                 ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto apply_shared_async(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<detail::serialized_access_mode::shared, true>(
            &self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that tries to invoke `f` as for `apply_shared`, completing with `busy_error`
    /// without invoking `f` if shared access cannot be acquired immediately.
    template <typename Self, typename F>
        requires detail::is_shared_gate<Gate> && ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> &&
                 ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto try_apply_shared(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<detail::serialized_access_mode::try_shared, false>(
            &self, ::std::forward<F>(f));
    }

    /// Returns a sender adaptor closure that tries to invoke `f` as for `apply_shared_async`, completing with
    /// `busy_error` without invoking `f` if shared access cannot be acquired immediately.
    template <typename Self, typename F>
        requires detail::is_shared_gate<Gate> && ::std::same_as<::std::remove_cvref_t<Self>, serialized_access> &&
                 ::std::is_lvalue_reference_v<Self&&>
    [[nodiscard]] auto try_apply_shared_async(this Self&& self, F&& f) {
        return detail::make_serialized_access_closure<detail::serialized_access_mode::try_shared, true>(
            &self, ::std::forward<F>(f));
    }

  private:
    /// Gate that controls access to `value_`.
    mutable Gate gate_;
    /// The value protected by `gate_`.
    T value_;

    template <detail::serialized_access_mode, bool, typename, typename>
    friend struct detail::serialized_access_closure;
};

} // namespace beman::gates

#endif

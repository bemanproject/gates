// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_DETAIL_SYNCHRONIZED_VALUE_HPP
#define BEMAN_GATES_DETAIL_SYNCHRONIZED_VALUE_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)
import beman.gates;
#else
    #include <beman/gates/detail/task_base.hpp>
#endif

#ifdef BEMAN_HAS_IMPORT_STD
import std;
#else
    #include <atomic>
    #include <concepts>
    #include <functional>
    #include <type_traits>
    #include <utility>
#endif

namespace beman::gates::detail {

/// A simple wrapper around a value of type `T` that provides synchronized access via a spinlock.
template <typename T>
struct synchronized_value {
    /// Constructs the contained `T` object with `std::forward<Args>(args)...`.
    template <typename... Args>
        requires ::std::constructible_from<T, Args...>
    explicit synchronized_value(::std::in_place_t, Args&&... args) : value_(::std::forward<Args>(args)...), lock_{} {}

    /// Applies `f` to the protected value in a synchronized manner.
    template <typename F>
        requires std::is_nothrow_invocable_v<F, T&>
    void apply(F&& f) {
        // Acquire the lock.
        while (lock_.test_and_set(std::memory_order_acquire)) {
            lock_.wait(true, std::memory_order_relaxed);
        }
        // Apply the function to the protected value.
        std::invoke(std::forward<F>(f), value_);
        // Release the lock.
        lock_.clear(std::memory_order_release);
        lock_.notify_one();
    }

  private:
    /// The value protected by the lock.
    T value_;
    /// Protects the queue state. Critical sections never execute user code.
    std::atomic_flag lock_{};
};

} // namespace beman::gates::detail

#endif

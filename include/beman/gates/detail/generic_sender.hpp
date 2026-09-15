// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#ifndef BEMAN_GATES_DETAIL_GENERIC_SENDER_HPP
#define BEMAN_GATES_DETAIL_GENERIC_SENDER_HPP

#include <beman/gates/config.hpp>

#if BEMAN_GATES_USE_MODULES() && !defined(BEMAN_GATES_INCLUDED_FROM_INTERFACE_UNIT)
import beman.execution;
#else
    #include <beman/execution/execution.hpp>
#endif

namespace beman::gates::detail {

/// A simple, generic sender, that wraps objects of type `Data`, completes with `Sigs`, and uses `State` to create the
/// operation state when connected to a receiver.
template <typename Data, typename Sigs, template <typename> typename State>
struct generic_sender {
    /// This is a sender.
    using sender_concept = ::beman::execution::sender_tag;
    /// The completion signatures of this sender.
    using completion_signatures = Sigs;

    /// Constructs `*this` with `data`.
    generic_sender(Data data) noexcept : data_{std::move(data)} {}

    /// Returns the async operation corresponding to `*this` connected to `receiver`.
    template <::beman::execution::receiver Receiver>
    State<Receiver> connect(Receiver&& receiver) const noexcept {
        return {std::forward<Receiver>(receiver), data_};
    }

    /// Returns the completion signatures of `*this`.
    template <typename, typename...>
    static consteval completion_signatures get_completion_signatures() noexcept {
        return {};
    }

  private:
    /// The data stored in the sender.
    Data data_;
};

} // namespace beman::gates::detail

#endif

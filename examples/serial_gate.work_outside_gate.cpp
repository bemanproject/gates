// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/serial_gate.hpp>
#include <beman/execution/execution.hpp>

#include <print>

using beman::gates::serial_gate;
using namespace beman::execution;

struct record {
    int data;
};

struct cache {
    void insert(record r) { std::print("insert called: {}\n", r.data); }
};

void perform_logging_after_update(record r) noexcept { std::print("Logging after update: {}\n", r.data); }

serial_gate    cache_gate;
cache          c;
counting_scope scope;

void on_record(record r) {
    auto update = just(std::move(r)) | then([&](record r) noexcept {
                      c.insert(r);
                      return r;
                  });

    auto protected_update = within(cache_gate.acquire(), std::move(update));

    // NEW
    auto with_logging = std::move(protected_update) | then(perform_logging_after_update);

    spawn(std::move(with_logging), scope.get_token());
}

int main() {
    constexpr auto N = 20;
    std::thread    threads[N];
    for (int i = 0; i < N; ++i) {
        threads[i] = std::thread([i]() { on_record({i}); });
    }
    for (int i = 0; i < N; ++i) {
        threads[i].join();
    }
    sync_wait(scope.join());
    return 0;
}

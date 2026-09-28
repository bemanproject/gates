// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/serial_gate.hpp>
#include <beman/execution/execution.hpp>

#include <print>

using beman::gates::serial_gate;
using namespace beman::execution;

struct connection {
    int id;
};

struct request {
    int data;
};
struct response {
    int data;
};

void prepare(request req) { std::print("preparing request: {}\n", req.data); }

auto read_response(request req, connection& conn) noexcept { return just(response{req.data + 10 * conn.id}); }

void process_response(const response& conn) noexcept { std::print("processing response: {}\n", conn.data); }

auto send_request(connection&, request req) { return just(std::move(req)); }

void continue_after_submit() noexcept { std::print("Continuing after submit\n"); }

serial_gate    connection_gate;
connection     conn{23};
counting_scope scope;

void submit_request(request req) {
    prepare(req); // does not need exclusive access to conn

    auto transaction = send_request(conn, std::move(req)) |
                       let_value([&](request req) noexcept { return read_response(std::move(req), conn); }) |
                       then(process_response);

    auto protected_transaction = within(connection_gate.acquire(), std::move(transaction));

    spawn(std::move(protected_transaction), scope.get_token());

    continue_after_submit(); // does not wait for the transaction to finish
}

int main() {
    constexpr auto N = 10;
    std::thread    threads[N];
    for (int i = 0; i < N; ++i) {
        threads[i] = std::thread([i]() { submit_request({i}); });
    }
    for (int i = 0; i < N; ++i) {
        threads[i].join();
    }
    sync_wait(scope.join());
    return 0;
}

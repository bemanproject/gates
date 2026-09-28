#include <beman/gates/serial_gate.hpp>
#include <beman/execution/execution.hpp>

#include <print>
#include <chrono>
#include <thread>

using beman::gates::serial_gate;
using namespace beman::execution;

struct document {
  int data;
};

auto save_async(document d) noexcept {
  std::print("saving document: {}\n", d.data);
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  return just();
}

serial_gate gate;
counting_scope scope;

void trigger_save(document d) {
  auto work
    = just(std::move(d))
    | let_value(save_async);

  auto protected_work =
    within(gate.acquire(), std::move(work));

  spawn(std::move(protected_work), scope.get_token());
}

int main() {
  constexpr auto N = 10;
  std::thread threads[N];
  for (int i = 0; i < N; ++i) {
    threads[i] = std::thread([i]() {
      trigger_save(document{i});
    });
  }
  for (int i = 0; i < N; ++i) {
    threads[i].join();
  }
  sync_wait(scope.join());
  return 0;
}
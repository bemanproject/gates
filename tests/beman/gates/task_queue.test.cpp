// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/detail/task_queue.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <semaphore>
#include <thread>
#include <vector>

using namespace beman::gates::detail;

namespace {

struct test_task : task_base {
    using callback = std::function<void(test_task*)>;

    explicit test_task(callback cb) : task_base{&execute}, callback_{cb} {}

    callback callback_;
    int      id_{0};

    static void execute(task_base* self) noexcept {
        auto* task = static_cast<test_task*>(self);
        task->callback_(task);
    }
};

} // namespace

TEST(TaskQueueTest, TaskStartsImmediatelyWhenQueueIsEmpty) {

    // Arrange
    task_queue sut;
    bool       executed{false};
    test_task  task{[&executed](test_task*) noexcept { executed = true; }};

    // Act
    sut.enqueue(&task);

    // Assert
    EXPECT_TRUE(executed);

    sut.on_task_complete();
    EXPECT_TRUE(executed);
}

TEST(TaskQueueTest, TaskWaitsUntilRunningTaskCompletes) {

    // Arrange
    task_queue sut;
    bool       first_executed{false};
    bool       second_executed{false};
    test_task  first{[&first_executed](test_task*) noexcept { first_executed = true; }};
    test_task  second{[&second_executed](test_task*) noexcept { second_executed = true; }};

    // Act
    sut.enqueue(&first);
    sut.enqueue(&second);

    // Assert
    EXPECT_TRUE(first_executed);
    EXPECT_FALSE(second_executed);

    sut.on_task_complete();
    EXPECT_TRUE(second_executed);

    sut.on_task_complete();
}

TEST(TaskQueueTest, TryEnqueueStartsImmediatelyAndReleasesReservation) {

    // Arrange
    task_queue  sut;
    std::size_t execution_count{0};
    test_task   first{[&execution_count](test_task*) noexcept { ++execution_count; }};
    test_task   second{[&execution_count](test_task*) noexcept { ++execution_count; }};

    // Act
    const bool first_started = sut.try_enqueue(&first);

    // Assert
    EXPECT_TRUE(first_started);
    EXPECT_EQ(execution_count, 1U);

    sut.on_task_complete();

    const bool second_started = sut.try_enqueue(&second);
    EXPECT_TRUE(second_started);
    EXPECT_EQ(execution_count, 2U);

    sut.on_task_complete();
}

TEST(TaskQueueTest, TryEnqueueFailsWhenTaskIsExecuting) {

    // Arrange
    task_queue            sut;
    std::binary_semaphore first_started{0};
    std::binary_semaphore release_first{0};
    std::atomic<bool>     speculative_executed{false};
    test_task             first{[&](test_task*) noexcept {
        first_started.release();
        release_first.acquire();
    }};
    test_task speculative{[&](test_task*) noexcept { speculative_executed.store(true, std::memory_order_relaxed); }};

    // Act
    std::thread first_thread{[&] { sut.enqueue(&first); }};
    first_started.acquire();
    const bool started = sut.try_enqueue(&speculative);
    release_first.release();
    first_thread.join();
    sut.on_task_complete();

    // Assert
    EXPECT_FALSE(started);
    EXPECT_FALSE(speculative_executed.load(std::memory_order_relaxed));
}

TEST(TaskQueueTest, TryEnqueueFailsWhenTaskIsQueued) {

    // Arrange
    task_queue            sut;
    std::binary_semaphore first_started{0};
    std::binary_semaphore release_first{0};
    std::atomic<bool>     second_executed{false};
    std::atomic<bool>     speculative_executed{false};
    test_task             first{[&](test_task*) noexcept {
        first_started.release();
        release_first.acquire();
    }};
    test_task             second{[&](test_task*) noexcept { second_executed.store(true, std::memory_order_relaxed); }};
    test_task speculative{[&](test_task*) noexcept { speculative_executed.store(true, std::memory_order_relaxed); }};

    // Act
    std::thread first_thread{[&] { sut.enqueue(&first); }};
    first_started.acquire();
    sut.enqueue(&second);
    const bool started = sut.try_enqueue(&speculative);
    release_first.release();
    first_thread.join();
    sut.on_task_complete();
    const bool second_was_executed = second_executed.load(std::memory_order_relaxed);
    sut.on_task_complete();

    // Assert
    EXPECT_FALSE(started);
    EXPECT_FALSE(speculative_executed.load(std::memory_order_relaxed));
    EXPECT_TRUE(second_was_executed);
}

TEST(TaskQueueTest, TryEnqueueQueuesNormalTasksWithoutOverlap) {

    // Arrange
    task_queue            sut;
    std::binary_semaphore speculative_started{0};
    std::binary_semaphore release_speculative{0};
    std::atomic<bool>     speculative_active{false};
    std::atomic<bool>     normal_executed{false};
    test_task             normal{[&](test_task*) noexcept { normal_executed.store(true, std::memory_order_release); }};
    test_task             speculative{[&](test_task*) noexcept {
        speculative_active.store(true, std::memory_order_release);
        speculative_started.release();
        sut.enqueue(&normal);
        EXPECT_FALSE(normal_executed.load(std::memory_order_acquire));
        release_speculative.acquire();
        speculative_active.store(false, std::memory_order_release);
    }};

    // Act
    bool        speculative_started_result{false};
    std::thread speculative_thread{[&] { speculative_started_result = sut.try_enqueue(&speculative); }};
    speculative_started.acquire();
    const bool normal_executed_while_speculative_active = normal_executed.load(std::memory_order_acquire);
    release_speculative.release();
    speculative_thread.join();
    sut.on_task_complete();
    const bool normal_executed_after_speculative_completion = normal_executed.load(std::memory_order_acquire);
    sut.on_task_complete();

    // Assert
    EXPECT_TRUE(speculative_started_result);
    EXPECT_FALSE(normal_executed_while_speculative_active);
    EXPECT_FALSE(speculative_active.load(std::memory_order_acquire));
    EXPECT_TRUE(normal_executed_after_speculative_completion);
}

TEST(TaskQueueTest, ConcurrentTryEnqueueHasSingleWinner) {

    // Arrange
    constexpr std::size_t contender_count = 8;

    task_queue                               sut;
    std::counting_semaphore<contender_count> contenders_ready{0};
    std::counting_semaphore<contender_count> start_contenders{0};
    std::atomic<std::size_t>                 started_count{0};
    std::array<bool, contender_count>        results{};
    std::vector<test_task>                   tasks;
    tasks.reserve(contender_count);
    for (std::size_t i = 0; i != contender_count; ++i) {
        tasks.emplace_back(
            [&started_count](test_task*) noexcept { started_count.fetch_add(1, std::memory_order_relaxed); });
    }

    // Act
    std::vector<std::thread> contenders;
    contenders.reserve(contender_count);
    for (std::size_t i = 0; i != contender_count; ++i) {
        contenders.emplace_back([&, i] {
            contenders_ready.release();
            start_contenders.acquire();
            results[i] = sut.try_enqueue(&tasks[i]);
        });
    }
    for (std::size_t i = 0; i != contender_count; ++i) {
        contenders_ready.acquire();
    }
    start_contenders.release(contender_count);
    for (auto& contender : contenders) {
        contender.join();
    }

    // Assert
    EXPECT_EQ(std::count(results.begin(), results.end(), true), 1U);
    EXPECT_EQ(started_count.load(std::memory_order_relaxed), 1U);

    sut.on_task_complete();
}

TEST(TaskQueueTest, ConcurrentTryAndNormalEnqueueExecuteEveryTaskOnce) {

    // Arrange
    constexpr std::size_t normal_count   = 8;
    constexpr std::size_t try_count      = 8;
    constexpr std::size_t producer_count = normal_count + try_count;

    task_queue                                  sut;
    std::counting_semaphore<producer_count>     producers_ready{0};
    std::counting_semaphore<producer_count>     start_producers{0};
    std::counting_semaphore<producer_count>     callback_started{0};
    std::counting_semaphore<producer_count>     release_callbacks{0};
    std::atomic<std::size_t>                    normal_executions{0};
    std::atomic<std::size_t>                    try_executions{0};
    std::atomic<std::size_t>                    overlap_count{0};
    std::atomic<bool>                           active_callback{false};
    std::array<std::atomic<bool>, normal_count> normal_finished{};
    std::array<std::atomic<bool>, try_count>    try_finished{};
    std::array<bool, try_count>                 try_results{};

    std::array<std::unique_ptr<std::binary_semaphore>, normal_count> normal_completed;
    std::array<std::unique_ptr<std::binary_semaphore>, try_count>    try_completed;
    for (std::size_t i = 0; i != normal_count; ++i) {
        normal_completed[i] = std::make_unique<std::binary_semaphore>(0);
    }
    for (std::size_t i = 0; i != try_count; ++i) {
        try_completed[i] = std::make_unique<std::binary_semaphore>(0);
    }

    auto callback_body = [&](std::atomic<bool>&        finished,
                             std::binary_semaphore&    completed,
                             std::atomic<std::size_t>& executions) noexcept {
        if (active_callback.exchange(true, std::memory_order_acq_rel)) {
            overlap_count.fetch_add(1, std::memory_order_relaxed);
        }
        executions.fetch_add(1, std::memory_order_relaxed);
        callback_started.release();
        release_callbacks.acquire();
        if (!active_callback.exchange(false, std::memory_order_acq_rel)) {
            overlap_count.fetch_add(1, std::memory_order_relaxed);
        }
        finished.store(true, std::memory_order_release);
        completed.release();
    };

    std::vector<test_task> normal_tasks;
    normal_tasks.reserve(normal_count);
    for (std::size_t i = 0; i != normal_count; ++i) {
        normal_tasks.emplace_back([&, i](test_task*) noexcept {
            callback_body(normal_finished[i], *normal_completed[i], normal_executions);
        });
    }

    std::vector<test_task> try_tasks;
    try_tasks.reserve(try_count);
    for (std::size_t i = 0; i != try_count; ++i) {
        try_tasks.emplace_back(
            [&, i](test_task*) noexcept { callback_body(try_finished[i], *try_completed[i], try_executions); });
    }

    // Act
    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t i = 0; i != normal_count; ++i) {
        producers.emplace_back([&, i] {
            producers_ready.release();
            start_producers.acquire();
            sut.enqueue(&normal_tasks[i]);
            normal_completed[i]->acquire();
            sut.on_task_complete();
        });
    }
    for (std::size_t i = 0; i != try_count; ++i) {
        producers.emplace_back([&, i] {
            producers_ready.release();
            start_producers.acquire();
            try_results[i] = sut.try_enqueue(&try_tasks[i]);
            if (try_results[i]) {
                sut.on_task_complete();
            }
        });
    }
    for (std::size_t i = 0; i != producer_count; ++i) {
        producers_ready.acquire();
    }
    start_producers.release(producer_count);
    callback_started.acquire();
    release_callbacks.release(producer_count);
    for (auto& producer : producers) {
        producer.join();
    }

    // Assert
    EXPECT_EQ(normal_executions.load(std::memory_order_relaxed), normal_count);
    EXPECT_EQ(try_executions.load(std::memory_order_relaxed),
              std::count(try_results.begin(), try_results.end(), true));
    EXPECT_EQ(overlap_count.load(std::memory_order_relaxed), 0U);
    for (const auto& finished : normal_finished) {
        EXPECT_TRUE(finished.load(std::memory_order_relaxed));
    }
    for (std::size_t i = 0; i != try_count; ++i) {
        EXPECT_EQ(try_finished[i].load(std::memory_order_relaxed), try_results[i]);
        EXPECT_EQ(try_completed[i]->try_acquire(), try_results[i]);
    }
}

TEST(TaskQueueTest, ReentrantEnqueueWaitsUntilRunningTaskCompletes) {

    // Arrange
    task_queue sut;
    bool       second_executed{false};
    test_task  second{[&second_executed](test_task*) noexcept { second_executed = true; }};
    test_task  first{[&sut, &second, &second_executed](test_task*) noexcept {
        sut.enqueue(&second);
        EXPECT_FALSE(second_executed);
    }};

    // Act
    sut.enqueue(&first);

    // Assert
    EXPECT_FALSE(second_executed);

    sut.on_task_complete();
    EXPECT_TRUE(second_executed);

    sut.on_task_complete();
}

TEST(TaskQueueTest, ReentrantCompletionStartsNextTask) {

    // Arrange
    task_queue sut;
    bool       second_executed{false};
    test_task  second{[&second_executed](test_task*) noexcept { second_executed = true; }};
    test_task  first{[&sut, &second, &second_executed](test_task*) noexcept {
        sut.enqueue(&second);
        EXPECT_FALSE(second_executed);

        sut.on_task_complete();
        EXPECT_TRUE(second_executed);
    }};

    // Act
    sut.enqueue(&first);

    // Assert
    EXPECT_TRUE(second_executed);

    sut.on_task_complete();
}

TEST(TaskQueueTest, ManyConcurrentProducersExecuteEveryTaskOnce) {

    // Arrange
    constexpr std::size_t producer_count     = 8;
    constexpr std::size_t tasks_per_producer = 64;
    constexpr std::size_t waiting_task_count = producer_count * tasks_per_producer;

    task_queue sut;

    std::atomic<std::size_t>             executed_count{0};
    std::atomic<bool>                    active_tasks{false};
    std::array<bool, waiting_task_count> task_executions{};

    test_task first{[](test_task*) noexcept {}};
    sut.enqueue(&first);

    std::vector<test_task> tasks;
    tasks.reserve(waiting_task_count);
    for (std::size_t i = 0; i != waiting_task_count; ++i) {
        tasks.emplace_back([&](test_task* self) noexcept {
            EXPECT_FALSE(active_tasks.exchange(true, std::memory_order_relaxed));
            task_executions[static_cast<std::size_t>(self->id_)] = true;
            executed_count.fetch_add(1, std::memory_order_relaxed);
            EXPECT_TRUE(active_tasks.exchange(false, std::memory_order_relaxed));
        });
        tasks.back().id_ = static_cast<int>(i);
    }

    // Act
    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t producer = 0; producer != producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            const std::size_t start = producer * tasks_per_producer;
            const std::size_t end   = start + tasks_per_producer;
            for (std::size_t i = start; i != end; ++i) {
                sut.enqueue(&tasks[i]);
            }
        });
    }

    for (auto& producer : producers) {
        producer.join();
    }

    EXPECT_EQ(executed_count.load(std::memory_order_relaxed), 0U);

    for (std::size_t i = 0; i != waiting_task_count; ++i) {
        sut.on_task_complete();
    }

    // Assert
    EXPECT_EQ(executed_count.load(std::memory_order_relaxed), waiting_task_count);
    for (const auto& executed : task_executions) {
        EXPECT_TRUE(executed);
    }

    sut.on_task_complete();
}

TEST(TaskQueueTest, ConcurrentProducersAndCompleterExecuteEveryTaskOnce) {

    // Arrange
    constexpr std::size_t producer_count     = 8;
    constexpr std::size_t tasks_per_producer = 64;
    constexpr std::size_t waiting_task_count = producer_count * tasks_per_producer;

    task_queue sut;

    std::atomic<std::size_t> started_count{0};
    std::atomic<std::size_t> completed_count{0};
    std::atomic<std::size_t> enqueued_count{0};
    std::atomic<std::size_t> overlap_count{0};
    std::atomic<bool>        active_callback{false};
    std::atomic<bool>        start_producers{false};

    std::array<std::atomic<std::size_t>, waiting_task_count> task_executions{};

    test_task first{[&](test_task*) noexcept { started_count.fetch_add(1, std::memory_order_relaxed); }};
    sut.enqueue(&first);

    std::vector<test_task> tasks;
    tasks.reserve(waiting_task_count);
    for (std::size_t i = 0; i != waiting_task_count; ++i) {
        tasks.emplace_back([&](test_task* self) noexcept {
            if (active_callback.exchange(true, std::memory_order_relaxed)) {
                overlap_count.fetch_add(1, std::memory_order_relaxed);
            }
            task_executions[static_cast<std::size_t>(self->id_)].fetch_add(1, std::memory_order_relaxed);
            started_count.fetch_add(1, std::memory_order_relaxed);
            if (!active_callback.exchange(false, std::memory_order_relaxed)) {
                overlap_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
        tasks.back().id_ = static_cast<int>(i);
    }

    std::vector<std::thread> producers;
    producers.reserve(producer_count);
    for (std::size_t producer = 0; producer != producer_count; ++producer) {
        producers.emplace_back([&, producer] {
            while (!start_producers.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }

            const std::size_t start = producer * tasks_per_producer;
            const std::size_t end   = start + tasks_per_producer;
            for (std::size_t i = start; i != end; ++i) {
                sut.enqueue(&tasks[i]);
                enqueued_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    start_producers.store(true, std::memory_order_release);
    while (enqueued_count.load(std::memory_order_relaxed) < producer_count) {
        std::this_thread::yield();
    }

    // Act
    std::thread completer{[&] {
        for (std::size_t completed = 0; completed != waiting_task_count + 1; ++completed) {
            while (started_count.load(std::memory_order_acquire) == completed_count.load(std::memory_order_relaxed)) {
                std::this_thread::yield();
            }

            completed_count.fetch_add(1, std::memory_order_relaxed);
            sut.on_task_complete();
        }
    }};

    for (auto& producer : producers) {
        producer.join();
    }
    completer.join();

    // Assert
    EXPECT_EQ(started_count.load(std::memory_order_relaxed), waiting_task_count + 1);
    EXPECT_EQ(completed_count.load(std::memory_order_relaxed), waiting_task_count + 1);
    EXPECT_EQ(overlap_count.load(std::memory_order_relaxed), 0U);
    for (const auto& executed : task_executions) {
        EXPECT_EQ(executed.load(std::memory_order_relaxed), 1U);
    }
}

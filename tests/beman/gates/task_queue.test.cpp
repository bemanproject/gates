// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <beman/gates/detail/task_queue.hpp>

#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
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

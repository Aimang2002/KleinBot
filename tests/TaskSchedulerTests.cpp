#include <gtest/gtest.h>

#include "utils/KeyedTaskScheduler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

namespace
{
bool waitUntil(const std::function<bool()> &condition,
               std::chrono::milliseconds timeout = std::chrono::seconds(3))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (condition())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return condition();
}

MessageExecutionOptions fixedOptions(std::size_t workers, std::size_t capacity = 32)
{
    MessageExecutionOptions options;
    options.initialWorkerThreads = workers;
    options.maxWorkerThreads = workers;
    options.maxPendingMessages = capacity;
    options.dynamicScaling = false;
    return options;
}
}

TEST(KeyedTaskSchedulerTest, SerializesTasksWithSameKeyInSubmissionOrder)
{
    KeyedTaskScheduler scheduler(fixedOptions(3));
    std::mutex resultMutex;
    std::vector<int> order;
    std::atomic<int> active{0};
    std::atomic<int> maximumActive{0};

    for (int index = 0; index < 3; ++index)
    {
        ASSERT_EQ(scheduler.submit(42, [&, index]() {
            const int currentActive = ++active;
            maximumActive.store(std::max(maximumActive.load(), currentActive));
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            {
                std::lock_guard<std::mutex> lock(resultMutex);
                order.push_back(index);
            }
            --active;
        }), TaskSubmitResult::Accepted);
    }

    ASSERT_TRUE(waitUntil([&]() { return scheduler.pendingCount() == 0; }));
    scheduler.shutdown();
    EXPECT_EQ(order, (std::vector<int>{0, 1, 2}));
    EXPECT_EQ(maximumActive.load(), 1);
}

TEST(KeyedTaskSchedulerTest, RunsDifferentKeysInParallel)
{
    KeyedTaskScheduler scheduler(fixedOptions(2));
    std::mutex barrierMutex;
    std::condition_variable barrierChanged;
    int started = 0;
    bool release = false;

    auto task = [&]() {
        std::unique_lock<std::mutex> lock(barrierMutex);
        ++started;
        barrierChanged.notify_all();
        barrierChanged.wait(lock, [&]() { return release; });
    };

    ASSERT_EQ(scheduler.submit(1, task), TaskSubmitResult::Accepted);
    ASSERT_EQ(scheduler.submit(2, task), TaskSubmitResult::Accepted);
    {
        std::unique_lock<std::mutex> lock(barrierMutex);
        ASSERT_TRUE(barrierChanged.wait_for(
            lock, std::chrono::seconds(2), [&]() { return started == 2; }));
        release = true;
    }
    barrierChanged.notify_all();
    scheduler.shutdown();
    EXPECT_EQ(scheduler.submit(3, []() {}), TaskSubmitResult::Stopping);
}

TEST(KeyedTaskSchedulerTest, RejectsTasksWhenGlobalCapacityIsFull)
{
    KeyedTaskScheduler scheduler(fixedOptions(1, 1));
    std::mutex barrierMutex;
    std::condition_variable barrierChanged;
    bool started = false;
    bool release = false;

    ASSERT_EQ(scheduler.submit(1, [&]() {
        std::unique_lock<std::mutex> lock(barrierMutex);
        started = true;
        barrierChanged.notify_all();
        barrierChanged.wait(lock, [&]() { return release; });
    }), TaskSubmitResult::Accepted);
    {
        std::unique_lock<std::mutex> lock(barrierMutex);
        ASSERT_TRUE(barrierChanged.wait_for(
            lock, std::chrono::seconds(2), [&]() { return started; }));
    }

    EXPECT_EQ(scheduler.submit(2, []() {}), TaskSubmitResult::Full);
    {
        std::lock_guard<std::mutex> lock(barrierMutex);
        release = true;
    }
    barrierChanged.notify_all();
    scheduler.shutdown();
}

TEST(KeyedTaskSchedulerTest, CancelsQueuedTasksDuringShutdown)
{
    KeyedTaskScheduler scheduler(fixedOptions(1, 4));
    std::mutex barrierMutex;
    std::condition_variable barrierChanged;
    bool started = false;
    bool release = false;
    std::atomic<int> queuedExecutions{0};

    ASSERT_EQ(scheduler.submit(1, [&]() {
        std::unique_lock<std::mutex> lock(barrierMutex);
        started = true;
        barrierChanged.notify_all();
        barrierChanged.wait(lock, [&]() { return release; });
    }), TaskSubmitResult::Accepted);
    ASSERT_EQ(scheduler.submit(1, [&]() { ++queuedExecutions; }), TaskSubmitResult::Accepted);
    ASSERT_EQ(scheduler.submit(2, [&]() { ++queuedExecutions; }), TaskSubmitResult::Accepted);

    {
        std::unique_lock<std::mutex> lock(barrierMutex);
        ASSERT_TRUE(barrierChanged.wait_for(
            lock, std::chrono::seconds(2), [&]() { return started; }));
    }

    auto shutdown = std::async(std::launch::async, [&]() { scheduler.shutdown(); });
    // 先等 stopping 置位且队列被清空（submit 返回 Stopping 可观测），再放行阻塞任务；
    // 否则 worker 可能在 shutdown 清队前取走下一个排队任务，形成时序竞态
    ASSERT_TRUE(waitUntil([&]() {
        return scheduler.submit(9, []() {}) == TaskSubmitResult::Stopping;
    }));
    {
        std::lock_guard<std::mutex> lock(barrierMutex);
        release = true;
    }
    barrierChanged.notify_all();

    ASSERT_EQ(shutdown.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    shutdown.get();
    EXPECT_EQ(queuedExecutions.load(), 0);
    EXPECT_EQ(scheduler.pendingCount(), 0U);
}

TEST(KeyedTaskSchedulerTest, ExpandsForIndependentKeysAndShrinksAfterIdleTimeout)
{
    MessageExecutionOptions options;
    options.initialWorkerThreads = 1;
    options.maxWorkerThreads = 3;
    options.maxPendingMessages = 8;
    options.workerIdleSeconds = 1;
    options.dynamicScaling = true;
    KeyedTaskScheduler scheduler(options);

    std::mutex barrierMutex;
    std::condition_variable barrierChanged;
    int started = 0;
    bool release = false;
    auto blockingTask = [&]() {
        std::unique_lock<std::mutex> lock(barrierMutex);
        ++started;
        barrierChanged.notify_all();
        barrierChanged.wait(lock, [&]() { return release; });
    };

    ASSERT_EQ(scheduler.submit(1, blockingTask), TaskSubmitResult::Accepted);
    ASSERT_TRUE(waitUntil([&]() {
        std::lock_guard<std::mutex> lock(barrierMutex);
        return started == 1;
    }));
    ASSERT_EQ(scheduler.submit(2, blockingTask), TaskSubmitResult::Accepted);
    ASSERT_EQ(scheduler.submit(3, blockingTask), TaskSubmitResult::Accepted);
    ASSERT_TRUE(waitUntil([&]() {
        std::lock_guard<std::mutex> lock(barrierMutex);
        return started == 3;
    }));
    EXPECT_EQ(scheduler.workerCount(), 3U);

    {
        std::lock_guard<std::mutex> lock(barrierMutex);
        release = true;
    }
    barrierChanged.notify_all();
    ASSERT_TRUE(waitUntil([&]() { return scheduler.pendingCount() == 0; }));
    EXPECT_TRUE(waitUntil([&]() { return scheduler.workerCount() == 1; },
                          std::chrono::seconds(3)));
    scheduler.shutdown();
}

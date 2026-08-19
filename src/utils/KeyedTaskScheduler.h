#ifndef KEYED_TASK_SCHEDULER_H
#define KEYED_TASK_SCHEDULER_H

#include "../Application/MessageExecutionOptions.h"

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

enum class TaskSubmitResult
{
    Accepted,
    Full,
    Stopping
};

class KeyedTaskScheduler
{
public:
    using TaskErrorHandler = std::function<void(std::exception_ptr)>;

    explicit KeyedTaskScheduler(MessageExecutionOptions options,
                                TaskErrorHandler errorHandler = {});
    ~KeyedTaskScheduler();

    KeyedTaskScheduler(const KeyedTaskScheduler &) = delete;
    KeyedTaskScheduler &operator=(const KeyedTaskScheduler &) = delete;

    TaskSubmitResult submit(std::uint64_t key, std::function<void()> task);
    void shutdown();

    std::size_t workerCount() const;
    std::size_t pendingCount() const;

private:
    struct Lane
    {
        std::deque<std::function<void()>> tasks;
        bool scheduled = false;
    };

    struct WorkerRecord
    {
        std::thread thread;
        std::shared_ptr<std::atomic<bool>> finished;
    };

    void startWorkerLocked();
    void scaleUpLocked();
    void workerLoop(const std::shared_ptr<std::atomic<bool>> &finished);
    void reapFinishedWorkers();

    MessageExecutionOptions options;
    TaskErrorHandler errorHandler;
    mutable std::mutex mutex;
    std::condition_variable taskAvailable;
    std::unordered_map<std::uint64_t, Lane> lanes;
    std::queue<std::uint64_t> readyKeys;
    std::vector<WorkerRecord> workers;
    std::size_t liveWorkers = 0;
    std::size_t idleWorkers = 0;
    std::size_t outstandingTasks = 0;
    bool stopping = false;
};

#endif

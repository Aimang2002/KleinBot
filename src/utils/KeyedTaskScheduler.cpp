#include "KeyedTaskScheduler.h"

#include <algorithm>
#include <chrono>
#include <utility>

KeyedTaskScheduler::KeyedTaskScheduler(MessageExecutionOptions options,
                                       TaskErrorHandler errorHandler)
    : options(std::move(options)), errorHandler(std::move(errorHandler))
{
    this->options.initialWorkerThreads =
        std::max<std::size_t>(1, this->options.initialWorkerThreads);
    this->options.maxWorkerThreads = std::max(
        this->options.initialWorkerThreads, this->options.maxWorkerThreads);
    this->options.maxPendingMessages =
        std::max<std::size_t>(1, this->options.maxPendingMessages);
    this->options.workerIdleSeconds =
        std::max<std::size_t>(1, this->options.workerIdleSeconds);

    workers.reserve(this->options.maxWorkerThreads);
    std::lock_guard<std::mutex> lock(mutex);
    for (std::size_t index = 0; index < this->options.initialWorkerThreads; ++index)
        startWorkerLocked();
}

KeyedTaskScheduler::~KeyedTaskScheduler()
{
    shutdown();
}

TaskSubmitResult KeyedTaskScheduler::submit(std::uint64_t key, std::function<void()> task)
{
    reapFinishedWorkers();
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping)
            return TaskSubmitResult::Stopping;
        if (outstandingTasks >= options.maxPendingMessages)
            return TaskSubmitResult::Full;

        Lane &lane = lanes[key];
        lane.tasks.push_back(std::move(task));
        ++outstandingTasks;
        if (!lane.scheduled)
        {
            lane.scheduled = true;
            readyKeys.push(key);
        }
        scaleUpLocked();
    }
    taskAvailable.notify_all();
    return TaskSubmitResult::Accepted;
}

void KeyedTaskScheduler::shutdown()
{
    std::vector<WorkerRecord> joiningWorkers;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (workers.empty())
            return;
        stopping = true;
        std::size_t cancelledTasks = 0;
        for (const auto &entry : lanes)
            cancelledTasks += entry.second.tasks.size();
        outstandingTasks -= cancelledTasks;
        lanes.clear();
        std::queue<std::uint64_t> emptyReadyKeys;
        readyKeys.swap(emptyReadyKeys);
        joiningWorkers.swap(workers);
    }
    taskAvailable.notify_all();

    for (WorkerRecord &worker : joiningWorkers)
    {
        if (worker.thread.joinable())
            worker.thread.join();
    }
}

std::size_t KeyedTaskScheduler::workerCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return liveWorkers;
}

std::size_t KeyedTaskScheduler::pendingCount() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return outstandingTasks;
}

void KeyedTaskScheduler::startWorkerLocked()
{
    auto finished = std::make_shared<std::atomic<bool>>(false);
    ++liveWorkers;
    workers.push_back({
        std::thread([this, finished]() { workerLoop(finished); }),
        std::move(finished)});
}

void KeyedTaskScheduler::scaleUpLocked()
{
    if (!options.dynamicScaling || liveWorkers >= options.maxWorkerThreads)
        return;

    const std::size_t waitingLanes = readyKeys.size();
    const std::size_t shortage = waitingLanes > idleWorkers
        ? waitingLanes - idleWorkers
        : 0;
    const std::size_t newWorkers = std::min(
        shortage, options.maxWorkerThreads - liveWorkers);
    for (std::size_t index = 0; index < newWorkers; ++index)
        startWorkerLocked();
}

void KeyedTaskScheduler::workerLoop(const std::shared_ptr<std::atomic<bool>> &finished)
{
    while (true)
    {
        std::function<void()> task;
        std::uint64_t key = 0;
        {
            std::unique_lock<std::mutex> lock(mutex);
            ++idleWorkers;
            bool ready = true;
            if (options.dynamicScaling && liveWorkers > options.initialWorkerThreads)
            {
                ready = taskAvailable.wait_for(
                    lock, std::chrono::seconds(options.workerIdleSeconds),
                    [this]() { return stopping || !readyKeys.empty(); });
            }
            else
            {
                taskAvailable.wait(lock, [this]() { return stopping || !readyKeys.empty(); });
            }
            --idleWorkers;

            if (stopping && readyKeys.empty())
            {
                --liveWorkers;
                finished->store(true);
                return;
            }
            if (!ready && readyKeys.empty() && liveWorkers > options.initialWorkerThreads)
            {
                --liveWorkers;
                finished->store(true);
                return;
            }
            if (readyKeys.empty())
                continue;

            key = readyKeys.front();
            readyKeys.pop();
            auto laneIterator = lanes.find(key);
            if (laneIterator == lanes.end() || laneIterator->second.tasks.empty())
                continue;
            task = std::move(laneIterator->second.tasks.front());
            laneIterator->second.tasks.pop_front();
        }

        try
        {
            task();
        }
        catch (...)
        {
            if (errorHandler)
                errorHandler(std::current_exception());
        }

        {
            std::lock_guard<std::mutex> lock(mutex);
            --outstandingTasks;
            auto laneIterator = lanes.find(key);
            if (laneIterator == lanes.end())
                continue;
            if (laneIterator->second.tasks.empty())
            {
                lanes.erase(laneIterator);
            }
            else
            {
                readyKeys.push(key);
                taskAvailable.notify_one();
            }
        }
    }
}

void KeyedTaskScheduler::reapFinishedWorkers()
{
    std::vector<std::thread> completed;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto iterator = workers.begin();
        while (iterator != workers.end())
        {
            if (iterator->finished->load())
            {
                completed.push_back(std::move(iterator->thread));
                iterator = workers.erase(iterator);
            }
            else
            {
                ++iterator;
            }
        }
    }
    for (std::thread &worker : completed)
    {
        if (worker.joinable())
            worker.join();
    }
}

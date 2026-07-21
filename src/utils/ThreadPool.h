#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

class ThreadPool
{
public:
    explicit ThreadPool(std::size_t workerCount)
    {
        if (workerCount == 0)
        {
            workerCount = 1;
        }

        workers.reserve(workerCount);
        for (std::size_t index = 0; index < workerCount; ++index)
        {
            workers.emplace_back([this]() { workerLoop(); });
        }
    }

    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    ~ThreadPool()
    {
        shutdown();
    }

    void submit(std::function<void()> task)
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (stopping)
            {
                throw std::runtime_error("thread pool is stopping");
            }
            tasks.push(std::move(task));
        }
        taskAvailable.notify_one();
    }

    void shutdown()
    {
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (stopping)
            {
                return;
            }
            stopping = true;
        }
        taskAvailable.notify_all();

        for (auto &worker : workers)
        {
            if (worker.joinable())
            {
                worker.join();
            }
        }
        workers.clear();
    }

private:
    void workerLoop()
    {
        while (true)
        {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(queueMutex);
                taskAvailable.wait(lock, [this]() { return stopping || !tasks.empty(); });
                if (stopping && tasks.empty())
                {
                    return;
                }
                task = std::move(tasks.front());
                tasks.pop();
            }
            task();
        }
    }

    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queueMutex;
    std::condition_variable taskAvailable;
    bool stopping = false;
};

#endif // THREAD_POOL_H

#ifndef MESSAGE_EXECUTION_OPTIONS_H
#define MESSAGE_EXECUTION_OPTIONS_H

#include <cstddef>

struct MessageExecutionOptions
{
    // 队列容量与空闲线程回收超时为程序内部定值，不开放给配置文件与面板
    static constexpr std::size_t kMaxPendingMessages = 1024;
    static constexpr std::size_t kWorkerIdleSeconds = 30;

    std::size_t initialWorkerThreads = 1;
    std::size_t maxWorkerThreads = 1;
    std::size_t maxPendingMessages = kMaxPendingMessages;
    std::size_t workerIdleSeconds = kWorkerIdleSeconds;
    bool dynamicScaling = false;
};

#endif

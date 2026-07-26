#ifndef MESSAGE_EXECUTION_OPTIONS_H
#define MESSAGE_EXECUTION_OPTIONS_H

#include <cstddef>

struct MessageExecutionOptions
{
    std::size_t initialWorkerThreads = 1;
    std::size_t maxWorkerThreads = 1;
    std::size_t maxPendingMessages = 1024;
    std::size_t workerIdleSeconds = 30;
    bool dynamicScaling = false;
};

#endif

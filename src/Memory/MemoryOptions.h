#ifndef MEMORY_OPTIONS_H
#define MEMORY_OPTIONS_H

#include <cstddef>
#include <string>

struct MemoryOptions
{
    bool enabled = true;
    std::string model;
    std::size_t batchTurns = 3;
    std::size_t idleSeconds = 20;
    std::size_t recallLimit = 8;
};

#endif

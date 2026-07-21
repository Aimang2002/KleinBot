#ifndef MEMORY_EXTRACTOR_H
#define MEMORY_EXTRACTOR_H

#include "MemoryItem.h"
#include "../ModelApiCaller/Dock.hpp"
#include "../ModelRegistry/ModelRegistry.h"
#include <vector>

class MemoryExtractor
{
public:
    MemoryExtractor(Dock &dock, const ModelRegistry &models) : dock(dock), models(models) {}
    std::vector<MemoryMutation> extract(uint64_t user_id, const std::vector<MemoryTurn> &turns);

private:
    Dock &dock;
    const ModelRegistry &models;
};

#endif // MEMORY_EXTRACTOR_H

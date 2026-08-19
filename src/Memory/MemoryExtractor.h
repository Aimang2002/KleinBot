#ifndef MEMORY_EXTRACTOR_H
#define MEMORY_EXTRACTOR_H

#include "MemoryItem.h"
#include "../ModelApiCaller/Dock.hpp"
#include "../ModelRegistry/ModelRegistry.h"
#include <string>
#include <vector>

class MemoryExtractor
{
public:
    MemoryExtractor(Dock &dock, const ModelRegistry &models, std::string modelName)
        : dock(dock), models(models), modelName(std::move(modelName)) {}
    std::vector<MemoryMutation> extract(uint64_t user_id, const std::vector<MemoryTurn> &turns);

private:
    Dock &dock;
    const ModelRegistry &models;
    std::string modelName;
};

#endif // MEMORY_EXTRACTOR_H

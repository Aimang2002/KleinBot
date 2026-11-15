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
    MemoryExtractor(Dock &dock, const ModelRegistry &models)
        : dock(dock), models(models) {}
    // modelName：本次提取用的模型名，由调用方按当前配置给出（memory.model 改了下一批即用新模型）
    std::vector<MemoryMutation> extract(uint64_t user_id, const std::vector<MemoryTurn> &turns,
                                        const std::string &modelName);

private:
    Dock &dock;
    const ModelRegistry &models;
};

#endif // MEMORY_EXTRACTOR_H

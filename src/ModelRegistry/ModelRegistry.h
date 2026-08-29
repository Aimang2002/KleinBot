#ifndef MODELREGISTRY_H
#define MODELREGISTRY_H

#include "ChatModel.h"
#include <string>
#include <vector>

class ModelRegistry
{
public:
    explicit ModelRegistry(std::string registryPath) : registryPath(std::move(registryPath)) { reload(); }
    const ChatModel *find(const std::string &modelName) const;
    const std::vector<ChatModel> &all() const;
    void reload();

private:
    std::string registryPath;
    std::vector<ChatModel> chatModels;
};

#endif // MODELREGISTRY_H
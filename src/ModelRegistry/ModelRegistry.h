#ifndef MODELREGISTRY_H
#define MODELREGISTRY_H

#include "ChatModel.h"

class ModelRegistry
{
public:
    ModelRegistry() { this->reload(); };
    const ChatModel *find(const std::string &modelName) const;
    const std::vector<ChatModel> &all() const;
    void reload();

private:
    std::vector<ChatModel> chatModels;
};

#endif // MODELREGISTRY_H
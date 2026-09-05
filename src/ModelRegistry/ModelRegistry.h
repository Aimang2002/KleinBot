#ifndef MODELREGISTRY_H
#define MODELREGISTRY_H

#include "ChatModel.h"
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class ModelRegistry
{
public:
    explicit ModelRegistry(std::string registryPath) : registryPath(std::move(registryPath)) { reload(); }
    // find/all 按值返回：注册表支持运行期热重载，返回内部指针会在重载时悬空
    std::optional<ChatModel> find(const std::string &modelName) const;
    std::vector<ChatModel> all() const;
    bool reload();

private:
    std::string registryPath;
    mutable std::mutex mutex;
    std::vector<ChatModel> chatModels;
};

#endif // MODELREGISTRY_H

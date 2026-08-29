#ifndef CHATMODEL_H
#define CHATMODEL_H

#include <unordered_set>
#include <string>

struct ChatModel
{
    std::unordered_set<std::string> modelList;

    // 能力按模型名标注（注册表 Capabilities.vision：布尔=旧版整组继承，加载时展开成名单；
    // 数组=按模型正向名单）。未来新能力沿用同一模式（如 toolsModels）
    std::unordered_set<std::string> visionModels;

    std::string api_key;
    std::string endpoint;
    std::string api_standard;

    bool hasVision(const std::string &modelName) const
    {
        return this->visionModels.count(modelName) != 0;
    }
};

#endif // CHATMODEL_H

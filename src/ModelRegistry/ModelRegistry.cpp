#include "ModelRegistry.h"
#include "../JsonParse/JsonParse.h"
#include "../Log/Log.h"
#include <fstream>

const ChatModel *ModelRegistry::find(const std::string &modelName) const
{
    for (const auto &chatModel : this->chatModels)
    {
        auto model = chatModel.modelList.find(modelName);
        if (model != chatModel.modelList.end())
        {
            return &chatModel;
        }
    }
    return nullptr;
}

const std::vector<ChatModel> &ModelRegistry::all() const
{
    return this->chatModels;
}

void ModelRegistry::reload()
{
    // 载入模型名称
    std::ifstream ifsJson(registryPath);
    if (!ifsJson.is_open())
    {
        LOG_ERROR("模型配置文件打开失败！请检查该文件是否存在。");
        return;
    }
    std::string json((std::istreambuf_iterator<char>(ifsJson)), std::istreambuf_iterator<char>());
    ifsJson.close();

    nlohmann::json document;
    try
    {
        document = nlohmann::json::parse(json);
        if (!document.contains("Models") || !document["Models"].is_array())
        {
            LOG_FATAL("JSON 数据中缺少 Models 字段或 Models 不是数组！");
            return;
        }

        // 遍历 Models 数组
        this->chatModels.clear();
        const nlohmann::json &models = document["Models"];
        for (int i = 0; i < models.size(); i++)
        {
            const nlohmann::json &model = models[i];
            ChatModel cm;
            // 提取 ModelName 或 name 字段
            if (model.contains("ModelName") && model["ModelName"].is_array())
            {
                const nlohmann::json &modelNamesArray = model["ModelName"];
                for (int j = 0; j < modelNamesArray.size(); j++)
                {
                    cm.modelList.insert(modelNamesArray[j]);
                }
            }
            else if (model.contains("name") && model["name"].is_array())
            {
                const nlohmann::json &modelNamesArray = model["name"];
                for (int j = 0; j < modelNamesArray.size(); j++)
                {
                    cm.modelList.insert(modelNamesArray[j]);
                }
            }

            // 提取其余字段
            cm.api_key = model.value("api_key", "");
            cm.endpoint = model.value("api_endpoint", "");
            cm.api_standard = model.value("APIStandard", "");
            const nlohmann::json *capabilities = nullptr;
            if (model.contains("Capabilities") && model["Capabilities"].is_object())
                capabilities = &model["Capabilities"];
            else if (model.contains("capabilities") && model["capabilities"].is_object())
                capabilities = &model["capabilities"];
            if (capabilities != nullptr)
                cm.capabilities.vision = capabilities->value("vision", false);
            chatModels.push_back(cm);
        }
    }
    catch (const std::exception &e)
    {
        LOG_FATAL("Models JSON 解析失败！error:" + std::string(e.what()));
        return;
    }
}

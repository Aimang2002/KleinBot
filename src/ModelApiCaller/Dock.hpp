#ifndef DOCK_H
#define DOCK_H

#include "OpenAIStandard/OpenAIStandard.h"
#include "../Message/Person.hpp"

class Dock
{
public:
    // 聊天请求
    OpenAIChatResponse RequestChat(const nlohmann::json &context, Person *user = nullptr)
    {
        nlohmann::json json_data;
        OpenAIChatResponse response;

        // 指定超参数
        if (user == nullptr)
        {
            LOG_WARNING("超参数使用默认值");
            json_data["model"] = user->user_models.first;
            json_data["messages"] = context;
            json_data["temperature"] = ConfigManager::getInstance().configVariable("temperature");
            json_data["frequency_penalty"] = ConfigManager::getInstance().configVariable("frequency_penalty");
            json_data["presence_penalty"] = ConfigManager::getInstance().configVariable("presence_penalty");
        }
        else
        {
            json_data["model"] = user->user_models.first;
            json_data["messages"] = context;
            json_data["temperature"] = user->temperature;
            json_data["frequency_penalty"] = user->frequency_penalty;
            json_data["presence_penalty"] = user->presence_penalty;
        }

        // 判断用户目前使用的模型调用对应的接口
        if (user->user_models.second[2] == "OpenAI")
        {
            std::string format = user->user_models.first + "\n";
            format.append(user->user_models.second[0] + "\n");
            format.append(user->user_models.second[1] + "\n");
            format.append(user->user_models.second[2] + "\n");
            response = openai.send_to_chat(json_data, user->user_models.second[1], user->user_models.second[0]);
        }
        else
        {
            LOG_ERROR("错误的API规范");
            response.choices_message_content = "你的服务API规范为：" + user->user_models.second[2] + "，当前并未支持";
        }
        return response;
    }

private:
    // API&endpoint空白字符去除
    std::string filterNonNormalChars(std::string str)
    {
        std::string result;
        for (char c : str)
        {
            if (std::isprint(c) && !std::isspace(c))
            {
                result += c;
            }
        }
        return result;
    }

private:
    OpenAIStandard openai;
};

#endif
#ifndef DOCK_H
#define DOCK_H

#include <iostream>
#include "OpenAIStandard/OpenAIStandard.h"
#include "../Message/Person.hpp"

class Dock
{
public:
    static void RequestGPT(std::string &data, Person *user = nullptr)
    {
        std::string json_data;

        // 指定超参数
        if (user == nullptr)
        {
            LOG_WARNING("超参数使用默认值");
            json_data = "{\"model\":\"" + user->user_models.first + "\",\"messages\":" + data + ",";
            json_data += "\"temperature\":" + CManager.configVariable("temperature") + ",";
            json_data += "\"top_p\":" + CManager.configVariable("top_p") + ",";
            json_data += "\"frequency_penalty\":" + CManager.configVariable("frequency_penalty") + ",";
            json_data += "\"presence_penalty\":" + CManager.configVariable("presence_penalty");
            json_data += "}";
        }
        else
        {
            json_data = "{\"model\":\"" + user->user_models.first + "\",\"messages\":" + data + ",";
            json_data += "\"temperature\":" + user->temperature + ",";
            json_data += "\"top_p\":" + user->top_p + ",";
            json_data += "\"frequency_penalty\":" + user->frequency_penalty + ",";
            json_data += "\"presence_penalty\":" + user->presence_penalty;
            json_data += "}";
        }

        if (user->user_models.first == CManager.configVariable("DRAW_MODEL"))
        {
            OpenAIStandard::send_to_draw(data, user->user_models.first, user->user_models.second[1], user->user_models.second[0]);
        }
        else if (user->user_models.second[2] == "OpenAI")
        {
            std::string format = user->user_models.first + "\n";
            format.append(user->user_models.second[0] + "\n");
            format.append(user->user_models.second[1] + "\n");
            format.append(user->user_models.second[2] + "\n");
            OpenAIStandard::send_to_chat(json_data, user->user_models.first, user->user_models.second[1], user->user_models.second[0]);
            data = json_data;
        }
        else
        {
            LOG_ERROR("错误的API规范");
            data = "你的服务API规范为：" + user->user_models.second[2] + "，当前并未支持";
        }
    }

private:
    // API&endpoint空白字符去除
    static std::string filterNonNormalChars(std::string str)
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
};

#endif
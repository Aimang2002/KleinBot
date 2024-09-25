#ifndef DOCK_H
#define DOCK_H

#include <iostream>
#include "OpenAIStandard/OpenAIStandard.h"
#include "../Message/Person.hpp"

class Dock
{
public:
    static void RequestGPT(string &data, const pair<string, string> model, Person *user = nullptr)
    {
        // 注意：模型复杂时可能会发生冲突
        // 模型调用：根据传入进来的模型分析应该调用的接口
        // 不是OpenAI模型也用OpenAI调用，但是需要更换端点
        string endpoint;
        string api_key;
        std::string json_data;

        // 指定超参数
        if (user == nullptr)
        {
            LOG_WARNING("超参数使用默认值");
            json_data = "{\"model\":\"" + model.first + "\",\"messages\":" + data + ",";
            json_data += "\"temperature\":" + CManager.configVariable("temperature") + ",";
            json_data += "\"top_p\":" + CManager.configVariable("top_p") + ",";
            json_data += "\"frequency_penalty\":" + CManager.configVariable("frequency_penalty") + ",";
            json_data += "\"presence_penalty\":" + CManager.configVariable("presence_penalty");
            json_data += "}";
        }
        else
        {
            json_data = "{\"model\":\"" + model.first + "\",\"messages\":" + data + ",";
            json_data += "\"temperature\":" + user->temperature + ",";
            json_data += "\"top_p\":" + user->top_p + ",";
            json_data += "\"frequency_penalty\":" + user->frequency_penalty + ",";
            json_data += "\"presence_penalty\":" + user->presence_penalty;
            json_data += "}";
        }

        // 获取特定API
        auto resultAPI = appointAPI(model.first);

        // 注意：这里的判断规则不能打乱
        if (model.first == CManager.configVariable("DRAW_MODEL"))
        {
            OpenAIStandard::send_to_draw(data, model.first, resultAPI.second, resultAPI.first);
        }
        else if (model.second == "OpenAI")
        {
            OpenAIStandard::send_to_chat(json_data, model.first, resultAPI.second, resultAPI.first);
            data = json_data;
        }
        else
        {
            LOG_ERROR("错误的服务厂商");
            data = "你的服务厂商为：" + model.second + "，当前并未支持";
        }
    }

private:
    // API&endpoint空白字符去除
    static string filterNonNormalChars(string str)
    {
        string result;
        for (char c : str)
        {
            if (std::isprint(c) && !std::isspace(c))
            {
                result += c;
            }
        }
        return result;
    }

    // 指定API
    static pair<string, string> appointAPI(string model) // 返回内容 first = key，second = endpoint
    {
        pair<string, string> p;
        p.first = CManager.configVariable("DEFAULT_MODEL_API_KEY");
        p.second = CManager.configVariable("DEFAULT_MODEL_ENDPOINT");

        if (model == CManager.configVariable("OTHER_MODEL"))
        {
            p.first = CManager.configVariable("OTHER_MODEL_API_KEY");
            p.second = CManager.configVariable("OTHER_MODEL_ENDPOINT");
        }
        else if (model == CManager.configVariable("DRAW_MODEL"))
        {
            p.first = CManager.configVariable("DRAW_MODEL_API_KEY");
            p.second = CManager.configVariable("DRAW_MODEL_ENDPOINT");
        }
        else if (model == CManager.configVariable("VISION_MODEL"))
        {
            p.first = CManager.configVariable("VISION_MODEL_API_KEY");
            p.second = CManager.configVariable("VISION_MODEL_ENDPOINT");
        }
        return p;
    }
};

#endif
#ifndef DOCK_H
#define DOCK_H

#include <iostream>
#include "OpenAIStandard/OpenAIStandard.h"
#include "../Message/Person.hpp"

class Dock
{
public:
    static void RequestGPT(string &data, const string model, Person *user = nullptr)
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
            json_data = "{\"model\":\"" + model + "\",\"messages\":" + data + ",";
            json_data += "\"temperature\":" + CManager.configVariable("temperature") + ",";
            json_data += "\"top_p\":" + CManager.configVariable("top_p") + ",";
            json_data += "\"frequency_penalty\":" + CManager.configVariable("frequency_penalty") + ",";
            json_data += "\"presence_penalty\":" + CManager.configVariable("presence_penalty");
            json_data += "}";
        }
        else
        {
            json_data = "{\"model\":\"" + model + "\",\"messages\":" + data + ",";
            json_data += "\"temperature\":" + user->temperature + ",";
            json_data += "\"top_p\":" + user->top_p + ",";
            json_data += "\"frequency_penalty\":" + user->frequency_penalty + ",";
            json_data += "\"presence_penalty\":" + user->presence_penalty;
            json_data += "}";
        }

        if (model.find("gpt") != string::npos && model.find("vision") == string::npos)
        {
            endpoint = CManager.configVariable("OPENAI_MODEL_ENDPOINT");
            api_key = CManager.configVariable("OPENAI_MODEL_API_KEY");
            OpenAIStandard::GPT(json_data, model, endpoint, api_key);
            data = json_data;
        }
        else if (model.find("RWKV") != string::npos || model.find("rwkv") != string::npos || model == CManager.configVariable("OTHER_DEFAULT_MODEL"))
        {
            endpoint = CManager.configVariable("OTHER_MODEL_ENDPOINT");
            api_key = CManager.configVariable("OTHER_MODEL_API_KEY");
            OpenAIStandard::GPT(json_data, model, endpoint, api_key);
            data = json_data;
        }
        else if (model == CManager.configVariable("DRAW_DEFAULT_MODEL"))
        {
            api_key = CManager.configVariable("DRAW_MODEL_API_KEY");
            endpoint = CManager.configVariable("DRAW_MODEL_ENDPOINT");
            OpenAIStandard::send_to_draw(data, model, endpoint, api_key);
        }
        else
        {
            LOG_WARNING(model + "为未识别的模型，可能会出现特殊情况...");
            endpoint = CManager.configVariable("OTHER_MODEL_ENDPOINT");
            api_key = CManager.configVariable("OTHER_MODEL_API_KEY");
            OpenAIStandard::GPT(json_data, model, endpoint, api_key);
            data = json_data;
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
};

#endif
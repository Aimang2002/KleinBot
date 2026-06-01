#ifndef DOCK_H
#define DOCK_H

#include "OpenAIStandard/OpenAIStandard.h"
#include "../Message/Person.hpp"
#include "../Log/Log.h"
#include <string>

class Dock
{
public:
    // 构造函数
    Dock() {}

    /**
     * @brief 聊天请求
     * @param context 上下文
     * @param user 用户
     * @return 返回一个被序列化的Json的结构体
     */
    OpenAIChatResponse RequestChat(const std::string &context, const Person &user)
    {
        nlohmann::json payload;
        payload["model"] = user.user_models.first;
        OpenAIChatResponse response;

        std::string api_key = user.user_models.second[0];
        std::string endpoint = user.user_models.second[1];

        payload["messages"] = nlohmann::json::parse(context);

        // 判断用户目前使用的模型调用对应的接口
        if (user.user_models.second[2] == "OpenAI")
        {
            std::string format = user.user_models.first + "\n";
            format.append(user.user_models.second[0] + "\n");
            format.append(user.user_models.second[1] + "\n");
            format.append(user.user_models.second[2] + "\n");
            response = openai.send_to_chat(endpoint, api_key, payload);
        }
        else
        {
            LOG_ERROR("错误的API规范");
            response.choices_message_content = "你的服务API规范为：" + user.user_models.second[2] + "，当前并未支持";
        }
        // response.choices_message_content = JsonParse::getInstance().toJson(response.choices_message_content);
        return response;
    }

    /**
     * @brief 图片分析请求
     * @param endpoint 接口地址
     * @param api_key API密钥
     * @param prompt 提示词
     * @param base64 需要分析的图片的base64编码
     *
     * @return 返回一个被序列化的Json的结构体
     */
    OpenAIVisionResponse RequestVision(const std::string &endpoint, const std::string &api_key, const std::string &model, const std::string &prompt, const std::string &base64)
    {
        OpenAIVisionResponse response;
        response = openai.send_to_vision(endpoint, api_key, model, prompt, base64);
        // response.choice_message_content = JsonParse::getInstance().toJson(response.choice_message_content);
        // response.choice_message_refusal = JsonParse::getInstance().toJson(response.choice_message_refusal);
        return response;
    }

    /**
     * @brief 图片生成请求
     * @param prompt 提示词
     * @param user 用户
     * @return 返回一个被序列化的Json的结构体
     */
    OpenAIImageResponse RequestDraw(const std::string endpoint, const std::string api_key, const std::string model, const std::string prompt)
    {
        OpenAIImageResponse response;
        response = openai.send_to_draw(endpoint, api_key, model, prompt);
        return response;
    }

    // 析构函数
    ~Dock() {}

private:
    /**
     * @brief 过滤非正常字符
     * @param str 输入字符串
     */
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
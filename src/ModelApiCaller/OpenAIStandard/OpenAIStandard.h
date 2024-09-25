

/*
 * 该模块需要重构
 */

#pragma once
#ifndef OPENAI_HPP
#define OPENAI_HPP

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <mutex>
#include <unistd.h>
#include <curl/curl.h>
#include "../../ConfigManager/ConfigManager.h"
// #include "../../JsonParse/JsonParse.h"

extern ConfigManager &CManager;
// extern JsonParse &JParsingClass;

// OpenAIStandard类
class OpenAIStandard
{
public:
    OpenAIStandard() = default;

    // 文本翻译
    static bool text_translate(std::string &text, const std::string model, std::string language, const string endpoint, const string api_key);

    // 调用聊天模型
    static bool send_to_chat(string &data, string models, const string endpoint, string api_key);

    // 调用视觉模型
    static bool send_to_vision(std::string &data, std::string &base64, string model, const string endpoint, const string api_key);

    // 调用dall-e-3模型
    static bool send_to_draw(std::string &prompt, string model, const string endpoint, const string api_key);

private:
    // 回调函数
    static size_t write_callback_chat(char *ptr, size_t size, size_t nmemb, void *userdata);

    // 消息完整性判断
    static bool isMessageComplete(string &message);

    // 超时判断
    static bool isTimeOut(string &message);

    // KEY错误判断
    static bool isKeyError(string &message);

    // API和端点修正
    static string filterNonNormalChars(string str);

    ~OpenAIStandard() = default;

private:
};

#endif
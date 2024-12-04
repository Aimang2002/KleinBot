

/*
 * 该模块需要重构
 */

#pragma once
#ifndef OPENAI_HPP
#define OPENAI_HPP

#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <vector>
#include <sstream>
#include <mutex>
#include <unistd.h>
#include <stack>
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
    static bool text_translate(std::string &text, const std::string model, std::string language, const std::string endpoint, const std::string api_key);

    // 调用聊天模型
    static bool send_to_chat(std::string &data, std::string models, const std::string endpoint, std::string api_key);

    // 调用视觉模型
    static bool send_to_vision(std::string &data, std::string &base64, std::string model, const std::string endpoint, const std::string api_key);

    // 调用dall-e-3模型
    static bool send_to_draw(std::string &prompt, std::string model, const std::string endpoint, const std::string api_key);

private:
    // 回调函数
    static size_t write_callback_chat(char *ptr, size_t size, size_t nmemb, std::string *userdata);

    // 消息完整性判断
    static bool isMessageComplete(std::string &message);

    // 超时判断
    static bool isTimeOut(std::string &message);

    // KEY错误判断
    static bool isKeyError(std::string &message);

    // API和端点修正
    static std::string filterNonNormalChars(std::string str);

    // Response json 合法性验证
    // static std::string ResponseJsonVerify(std::string str, std::string sub = "}}");

    // 证证书合法性(windows下)
    static void VerifyCertificate(CURL *curl);

    ~OpenAIStandard() = default;

private:
};

#endif

#pragma once
#ifndef OPENAI_HPP
#define OPENAI_HPP

#include <iostream>
#include "../../ConfigManager/ConfigManager.h"
#include <curl/curl.h>

// 聊天响应结构体
struct OpenAIChatResponse
{
    int code;
    std::string id;                      // API请求唯一标识符
    std::string object;                  // 对象类型，固定为"chat.completion"
    int created;                         // 请求创建时间（Unix时间戳，秒）
    std::string model;                   // 使用的模型名称，如"gpt-4-turbo"
    int choices_index;                   // 回复选项索引（多候选回复时使用，通常为0）
    std::string choices_message_role;    // 回复消息的角色，固定为"assistant"
    std::string choices_message_content; // 助理回复的实际内容
    std::string choices_finish_reason;   // 停止生成的原因，如"stop"、"length"
    int usage_prompt_tokens;             // 输入提示消耗的token数量
    int usage_completion_tokens;         // 生成回复消耗的token数量
    int usage_total_tokens;              // 总共消耗的token数量
};

// 图片创造响应结构体
struct OpenAIImageResponse
{
    // 基础信息
    int code;
    int created;

    // 输出内容
    std::string data_base64;
    std::string output_format;
    std::string quality;
    std::string size;

    // Token使用统计
    int usage_input_tokens;
    int usage_output_tokens;
    int usage_total_tokens;
};

struct OpenAIVisionResponse
{
    int code;
    std::string id;    // 请求的唯一标识符
    std::string model; // 使用的模型名称
    int created;       // 创建时间戳（Unix时间戳）

    std::string choice_message_content; // 返回的描述内容
    std::string choice_message_refusal; // 当content为null时，返回的拒绝理由

    int usage_completion_tokens; // 输出token数
    int usage_prompt_tokens;     // 输入token数
    int usage_total_tokens;      // 总token数
};

// OpenAIStandard类
class OpenAIStandard
{
public:
    OpenAIStandard() = default;

    // 文本翻译
    bool text_translate(std::string &text, const std::string model, std::string language, const std::string endpoint, const std::string api_key);

    // 调用聊天模型
    OpenAIChatResponse send_to_chat(const nlohmann::json &body, const std::string endpoint, std::string api_key);

    // 调用视觉模型
    OpenAIVisionResponse send_to_vision(const std::string &data, const std::string &base64, std::string model, const std::string endpoint, const std::string api_key);

    // 调用dall-e-3模型
    OpenAIImageResponse send_to_draw(const std::string &prompt, std::string model, const std::string endpoint, const std::string api_key);

private:
    /**
     * @brief 解析 OpenAI Chat Completion API 返回的 JSON 字符串
     *
     * 该函数将 OpenAI API 返回的 JSON 数据解析为 OpenAIChatResponse 结构体。
     * - 对字符串字段，如果 JSON 缺失则使用空字符串 "" 作为默认值。
     * - 对整数字段，如果 JSON 缺失则使用 0 作为默认值。
     * - 只处理单个 choice，忽略其他可能存在的候选回复。
     * - 当 JSON 格式错误或解析失败时，会记录错误日志并返回空的 OpenAIChatResponse。
     *
     * @param response JSON 格式的字符串，通常来自 OpenAI Chat Completion API 响应。
     * @return OpenAIChatResponse 解析后的结果对象，如果解析失败则返回空对象。
     *
     * @note 该函数对 JSON 的健壮性做了处理：
     *       - usage 字段缺失不会抛异常
     *       - message 字段缺失或结构异常不会抛异常
     *       - choices 数组为空也不会抛异常
     */
    OpenAIChatResponse chat_json_parse(const std::string &response);

    /**
     * @brief 解析 OpenAI Image API 返回的 JSON 字符串
     *
     * 该函数将 OpenAI API 返回的图像生成 JSON 数据解析为 OpenAIImageResponse 结构体。
     * - 对字符串字段，如果 JSON 缺失则使用空字符串 "" 作为默认值。
     * - 对整数字段，如果 JSON 缺失则使用 0 作为默认值。
     * - 只取 data 数组中的第一个图片（b64_json）。
     * - 当 JSON 格式错误或解析失败时，会记录错误并返回空的 OpenAIImageResponse。
     *
     * @param response JSON 格式的字符串，通常来自 OpenAI Image API 响应。
     * @return OpenAIImageResponse 解析后的结果对象，如果解析失败则返回空对象。
     *
     * @note 该函数对 JSON 的健壮性做了处理：
     *       - data 数组为空或缺失不会抛异常
     *       - usage 对象缺失或字段缺失不会抛异常
     *       - 类型不匹配（string/int）会使用默认值
     */
    OpenAIImageResponse draw_json_parse(const std::string &response);

    OpenAIVisionResponse vision_json_parse(const std::string &response);

    // 回调函数
    size_t write_callback_chat(char *ptr, size_t size, size_t nmemb, std::string *userdata);

    // KEY错误判断
    bool isKeyError(std::string &message);

    // API和端点修正
    std::string filterNonNormalChars(std::string str);

    // Response json 合法性验证
    //  std::string ResponseJsonVerify(std::string str, std::string sub = "}}");

    // 证证书合法性(windows下)
    void VerifyCertificate(CURL *curl);

    ~OpenAIStandard() = default;

private:
};

#endif
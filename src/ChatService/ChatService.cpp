#include "ChatService.h"
#include "../ConfigManager/ConfigManager.h"
#include "../Tool/ToolContext.h"

std::string ChatService::reply(uint64_t user_id, const std::string &text, bool use_context)
{
    // 1. 上下文模式：增量追加用户这句（内存 + SQLite 同步落盘）
    if (use_context)
    {
        this->userSession.appendMessage(user_id, "user", text);
    }

    // 2. 构造请求包（USS 负责裁切、查模型、拼超参数）
    auto bundleOpt = this->userSession.buildChatRequest(user_id);
    if (!bundleOpt)
    {
        return "系统提示：当前模型未注册，请管理员检查 ModelsName.json。";
    }
    auto &bundle = *bundleOpt;

    // 非上下文模式：清空历史，只发当前这条
    if (!use_context)
    {
        LOG_INFO("当前聊天不支持上下文模式...");
        bundle.request.history.clear();
        bundle.request.history.push_back({"user", text});
    }

    // 3. 塞入可用工具
    bundle.request.tools = this->tools.allSchemas();

    // 4. 调用 LLM + 工具调用循环
    std::cout << "Send to model..." << std::endl;
    ChatResponse response = this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);

    int rounds = 0;
    while (response.code == 200 && response.finish_reason == "tool_calls" && !response.tool_calls.empty())
    {
        if (++rounds > max_tool_rounds)
        {
            LOG_WARNING("工具调用轮次超过上限，强制结束");
            return "系统提示：处理超时，请重试。";
        }

        // 4a. 把 assistant 的工具调用请求追加进临时 history
        ChatMessage assistantMsg;
        assistantMsg.role = "assistant";
        assistantMsg.content = response.content;
        for (const auto &call : response.tool_calls)
        {
            assistantMsg.tool_calls.push_back({call.id, call.name, call.arguments});
        }
        bundle.request.history.push_back(assistantMsg);

        // 4b. 逐个执行工具，结果作为 role=="tool" 消息回灌
        for (const auto &call : response.tool_calls)
        {
            std::string result;
            Tool *tool = this->tools.find(call.name);
            if (tool == nullptr)
            {
                result = "错误：未知工具 " + call.name;
                LOG_WARNING("模型调用了未注册的工具：" + call.name);
            }
            else
            {
                try
                {
                    result = tool->execute(call.arguments, ToolContext{user_id});
                }
                catch (const std::exception &e)
                {
                    result = "错误：工具执行失败 " + std::string(e.what());
                    LOG_ERROR("工具 " + call.name + " 执行异常：" + std::string(e.what()));
                }
            }
            ChatMessage toolMsg;
            toolMsg.role = "tool";
            toolMsg.tool_call_id = call.id;
            toolMsg.content = result;
            bundle.request.history.push_back(toolMsg);
        }

        // 4c. 带着加长的 history 再次请求
        response = this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);
    }

    // 5. 错误处理
    if (response.code != 200)
    {
        LOG_ERROR("LLM response error code: " + std::to_string(response.code));
        return response.error_message.empty() ? std::string("系统提示：模型无返回内容！") : "系统提示：" + response.error_message;
    }

    std::string LLM_content = response.content;
    if (LLM_content.empty())
    {
        return "系统提示：模型无返回内容！";
    }

    // 5. 上下文模式：增量追加助手回复（内存 + SQLite 同步落盘）
    if (use_context)
    {
        this->userSession.appendMessage(user_id, "assistant", LLM_content);
    }

    // 6. finish_reason 附加提示
    if (response.finish_reason == "length")
    {
        LOG_WARNING("该模型的回答长度超出了管理员设定的最大限制...");
        LLM_content += "\n模型已超出最大长度限制，回答可能不全。";
    }
    else if (response.finish_reason == "content_filter")
    {
        LOG_WARNING("该模型的回答内容被AI morality filter拦截...");
        LLM_content += "\n模型返回内容被AI morality filter拦截...";
    }

    std::cout << "\033[32m" << "Model response: " << "\033[0m" << LLM_content << std::endl;
    return LLM_content;
}

std::string ChatService::replyOneShot(const std::string &prompt)
{
    std::string modelName = ConfigManager::getInstance().configVariable("DEFAULT_MODEL");

    const ChatModel *modelPtr = this->models.find(modelName);
    if (modelPtr == nullptr)
    {
        LOG_ERROR("DEFAULT_MODEL 未在 ModelsName.json 注册：" + modelName);
        return "系统提示：默认模型未配置";
    }

    ChatRequest request;
    request.frequency_penalty = std::stod(ConfigManager::getInstance().configVariable("frequency_penalty"));
    request.presence_penalty = std::stod(ConfigManager::getInstance().configVariable("presence_penalty"));
    request.temperature = std::stod(ConfigManager::getInstance().configVariable("temperature"));
    request.system_prompt = "你是人工助手";
    request.history.push_back({"user", prompt});

    ChatResponse response = this->dock.RequestChat(*modelPtr, modelName, request);

    if (response.code != 200)
    {
        LOG_ERROR("ChatService::replyOneShot 调用 LLM 失败：" + std::to_string(response.code));
        return response.error_message.empty() ? "网络异常" : response.error_message;
    }
    return response.content;
}

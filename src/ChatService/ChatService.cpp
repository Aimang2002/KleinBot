#include "ChatService.h"
#include "../Memory/MemoryService.h"
#include "../Tool/ToolContext.h"

ChatReply ChatService::reply(uint64_t user_id, const std::string &text, bool use_context)
{
    ChatReply resultReply;
    int64_t userMessageId = 0;
    // 1. 上下文模式：增量追加用户这句（内存 + SQLite 同步落盘）
    if (use_context)
    {
        userMessageId = this->userSession.appendMessage(user_id, "user", text);
        resultReply.user_message_id = userMessageId;
    }

    // 2. 构造请求包（USS 负责裁切、查模型、拼超参数）
    auto bundleOpt = this->userSession.buildChatRequest(user_id);
    if (!bundleOpt)
    {
        resultReply.text = "系统提示：当前模型未注册，请管理员检查 ModelsName.json。";
        return resultReply;
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
    bundle.request.system_prompt +=
        "\n\n图片上下文规则：历史中的 [image asset_id=...] 只是资源引用，你当前并未直接看到图片。"
        "当用户询问图片内容、位置、文字、颜色或细节时，必须调用 inspect_image；"
        "当用户要求重新发送历史图片时调用 send_image；当用户要求生成图片时调用 generate_image。"
        "不得在未调用 inspect_image 时猜测图片细节。"
        "当用户询问当前模型时调用 get_current_model；当用户要求开启或关闭语音时调用 set_voice_mode。"
        "管理员控制操作只能调用 admin_control，系统会在工具执行前校验管理员身份。"
        "不要把重置对话、设置人格或还原人格转换为工具调用。";

    // 4. 调用 LLM + 工具调用循环
    std::cout << "Send to model..." << std::endl;
    ChatResponse response = this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);
    if (response.cancelled)
        return resultReply;

    int rounds = 0;
    std::vector<std::string> contextAnnotations;
    bool terminalToolResult = false;
    std::string terminalToolContent;
    while (response.code == 200 && response.finish_reason == "tool_calls" && !response.tool_calls.empty())
    {
        if (++rounds > max_tool_rounds)
        {
            LOG_WARNING("工具调用轮次超过上限，强制结束");
            resultReply.text = "系统提示：处理超时，请重试。";
            return resultReply;
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
            ToolResult toolResult;
            Tool *tool = this->tools.find(call.name);
            if (tool == nullptr)
            {
                toolResult.model_content = "错误：未知工具 " + call.name;
                LOG_WARNING("模型调用了未注册的工具：" + call.name);
            }
            else
            {
                if (tool->requiresAdmin() && user_id != managerId)
                {
                    toolResult = {"权限不足：该操作仅限管理员。", {}, {}, true};
                }
                else try
                {
                    toolResult = tool->execute(call.arguments, ToolContext{user_id, userMessageId});
                }
                catch (const std::exception &e)
                {
                    toolResult.model_content = "错误：工具执行失败 " + std::string(e.what());
                    LOG_ERROR("工具 " + call.name + " 执行异常：" + std::string(e.what()));
                }
            }
            if (toolResult.cancelled)
                return resultReply;
            ChatMessage toolMsg;
            toolMsg.role = "tool";
            toolMsg.tool_call_id = call.id;
            toolMsg.content = toolResult.model_content;
            bundle.request.history.push_back(toolMsg);
            resultReply.outbound_messages.insert(resultReply.outbound_messages.end(),
                                                  toolResult.outbound_messages.begin(),
                                                  toolResult.outbound_messages.end());
            if (!toolResult.context_content.empty())
                contextAnnotations.push_back(toolResult.context_content);
            if (toolResult.terminal)
            {
                terminalToolResult = true;
                terminalToolContent = toolResult.model_content;
            }
        }

        if (terminalToolResult)
            break;

        // 4c. 带着加长的 history 再次请求
        response = this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);
        if (response.cancelled)
            return resultReply;
    }

    // 5. 错误处理
    if (response.code != 200 && !terminalToolResult)
    {
        LOG_ERROR("LLM response error code: " + std::to_string(response.code));
        resultReply.text = response.error_message.empty() ? std::string("系统提示：模型无返回内容！") : "系统提示：" + response.error_message;
        return resultReply;
    }

    std::string LLM_content = terminalToolResult ? terminalToolContent : response.content;
    if (LLM_content.empty())
    {
        resultReply.text = "系统提示：模型无返回内容！";
        return resultReply;
    }

    // 5. 上下文模式：增量追加助手回复（内存 + SQLite 同步落盘）
    if (use_context)
    {
        std::string persistedAssistantContent = LLM_content;
        for (const auto &annotation : contextAnnotations)
            persistedAssistantContent += "\n" + annotation;
        const int64_t assistantMessageId = this->userSession.appendMessage(
            user_id, "assistant", persistedAssistantContent);
        this->memoryService.enqueueTurn(
            user_id, text, LLM_content, userMessageId, assistantMessageId);
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
    resultReply.text = LLM_content;
    return resultReply;
}

std::string ChatService::replyOneShot(const std::string &prompt)
{
    const std::string &modelName = chatConfig.defaultModel;

    const ChatModel *modelPtr = this->models.find(modelName);
    if (modelPtr == nullptr)
    {
        LOG_ERROR("DEFAULT_MODEL 未在 ModelsName.json 注册：" + modelName);
        return "系统提示：默认模型未配置";
    }

    ChatRequest request;
    request.frequency_penalty = chatConfig.frequencyPenalty;
    request.presence_penalty = chatConfig.presencePenalty;
    request.temperature = chatConfig.temperature;
    request.system_prompt = "你是人工助手";
    request.history.push_back({"user", prompt});

    ChatResponse response = this->dock.RequestChat(*modelPtr, modelName, request);

    if (response.cancelled)
        return {};

    if (response.code != 200)
    {
        LOG_ERROR("ChatService::replyOneShot 调用 LLM 失败：" + std::to_string(response.code));
        return response.error_message.empty() ? "网络异常" : response.error_message;
    }
    return response.content;
}

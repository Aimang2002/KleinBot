#include "ChatService.h"
#include "../ConfigManager/ConfigManager.h"

std::string ChatService::reply(uint64_t user_id, const std::string &text, bool use_context)
{
    // 1. 上下文模式：先把用户这句追加到历史
    if (use_context)
    {
        auto history = this->userSession.getChatHistory(user_id);
        history.push_back({"user", text, time(nullptr)});
        this->userSession.updateChatHistory(user_id, history);
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

    // 3. 调用 LLM
    std::cout << "Send to model..." << std::endl;
    auto response = this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);

    // 4. 错误处理
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

    // 5. 上下文模式：把回复也写回历史
    if (use_context)
    {
        auto history = this->userSession.getChatHistory(user_id);
        history.push_back({"assistant", LLM_content, time(nullptr)});
        this->userSession.updateChatHistory(user_id, history);
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

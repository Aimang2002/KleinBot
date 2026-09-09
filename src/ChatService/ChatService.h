#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include "../ModelApiCaller/Dock.hpp"
#include "../UserSession/UserSessionService.h"
#include "../ModelRegistry/ModelRegistry.h"
#include "ChatOptions.h"
#include "../Tool/ToolRegistry.h"
#include "../Port/OutboundMessage.h"
#include "../Port/ChatRequest.h"
#include <optional>
#include <string>
#include <vector>

class MemoryService;

struct ChatReply
{
    std::string text;
    std::vector<OutboundMessage> outbound_messages;
    int64_t user_message_id = 0;
};

class ChatService
{
public:
    ChatService(Dock &dock, UserSessionService &USS, const ModelRegistry &models,
                const ToolRegistry &tools, MemoryService &memoryService,
                const ChatOptions &chatConfig, uint64_t managerId)
        : dock(dock), userSession(USS), models(models), tools(tools), memoryService(memoryService),
          chatConfig(chatConfig), managerId(managerId) {}
    // situationNote：单轮情境注记（如新朋友第一句话），追加在 system prompt 内——
    // 行为约束归 system 而非用户文本（约束力更强）；仅影响注入的那一轮，
    // 供应商前缀缓存代价为该用户一次性 miss
    ChatReply reply(uint64_t user_id, const std::string &text, bool use_context,
                    std::optional<ChatImageContent> currentImage = std::nullopt,
                    const std::string &situationNote = {});
    // 人格化单轮回应（T6）：与 reply() 相同的人格装配（用户人格 / soul 兜底 + 服务契约），
    // 但单轮、不带工具、不写会话不入长期记忆——戳一戳、欢迎、提醒转达等被动场景不值得进记忆。
    // 失败返回空串，调用方决定降级，绝不把错误文案当回复发给用户
    std::string replyInCharacter(uint64_t user_id, const std::string &prompt);
    // 一次性调用（自定 system，低温度）：人格编译等后台任务用。
    // 不写历史、不入记忆队列、无工具；失败返回空串
    std::string buildOnce(const std::string &systemPrompt, const std::string &userPrompt);

private:
    Dock &dock;
    UserSessionService &userSession;
    const ModelRegistry &models;
    const ToolRegistry &tools;
    MemoryService &memoryService;
    ChatOptions chatConfig;
    uint64_t managerId;

    static constexpr int max_tool_rounds = 5; // 防工具调用死循环
};

#endif // CHATSERVICE_H

#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include "../ModelApiCaller/Dock.hpp"
#include "../UserSession/UserSessionService.h"
#include "../ModelRegistry/ModelRegistry.h"
#include "../Configuration/AppConfig.h"
#include "../Tool/ToolRegistry.h"
#include "../Port/OutboundMessage.h"
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
                const ChatConfig &chatConfig, uint64_t managerId)
        : dock(dock), userSession(USS), models(models), tools(tools), memoryService(memoryService),
          chatConfig(chatConfig), managerId(managerId) {}
    ChatReply reply(uint64_t user_id, const std::string &text, bool use_context);
    std::string replyOneShot(const std::string &prompt); // 无状态（一次性）对话

private:
    Dock &dock;
    UserSessionService &userSession;
    const ModelRegistry &models;
    const ToolRegistry &tools;
    MemoryService &memoryService;
    ChatConfig chatConfig;
    uint64_t managerId;

    static constexpr int max_tool_rounds = 5; // 防工具调用死循环
};

#endif // CHATSERVICE_H

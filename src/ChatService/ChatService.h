#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include "../ModelApiCaller/Dock.hpp"
#include "../UserSession/UserSessionService.h"
#include "../ModelRegistry/ModelRegistry.h"
#include <string>

class ChatService
{
public:
    ChatService(Dock &dock, UserSessionService &USS, const ModelRegistry &models) : dock(dock), userSession(USS), models(models) {}
    std::string reply(uint64_t user_id, const std::string &text, bool use_context);
    std::string replyOneShot(const std::string &prompt); // 无状态（一次性）对话

private:
    Dock &dock;
    UserSessionService &userSession;
    const ModelRegistry &models;
};

#endif // CHATSERVICE_H

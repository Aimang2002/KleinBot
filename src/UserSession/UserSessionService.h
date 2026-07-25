#ifndef USERSESSIONSERVICE_H
#define USERSESSIONSERVICE_H

#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include "../Message/Person.hpp"
#include "../ModelRegistry/ModelRegistry.h"
#include "../ModelRegistry/ChatModel.h"
#include "../Configuration/AppConfig.h"
#include "../Port/ChatRequest.h"

class ConversationStore;
class MemoryService;
class ImageAssetStore;

struct ChatCallBundle
{
    ChatModel model;
    std::string model_name;
    ChatRequest request;
};

class UserSessionService
{
public:
    UserSessionService(const ModelRegistry &mr, ConversationStore &store, const BotConfig &bot, const ChatConfig &chat);
    void setMemoryService(MemoryService *service);
    void setImageAssetStore(ImageAssetStore *store);
    void ensureUserExists(const uint64_t user_id);
    void resetChat(const uint64_t user_id);
    std::string getModelName(uint64_t user_id);
    void setPersonality(const uint64_t user_id, const std::string &Personality);
    void resetPersonality(const uint64_t user_id);
    void switchModel(const uint64_t user_id, const std::string &modelName);
    void voiceSwitch(const uint64_t user_id, const bool tag);
    bool isVoiceMode(const uint64_t user_id);
    std::string removePreviousContext(const uint64_t user_id);
    std::vector<TimestampedMessage> getChatHistory(const uint64_t user_id);
    void updateChatHistory(const uint64_t user_id, const std::vector<TimestampedMessage> &history);
    int64_t appendMessage(const uint64_t user_id, const std::string &role, const std::string &content);
    Person getUserConfig(const uint64_t user_id);
    std::optional<ChatCallBundle> buildChatRequest(const uint64_t &user_id);

private:
    std::mutex mutex_message;
    std::string default_personality;
    std::unique_ptr<std::unordered_map<uint64_t, Person>> user_messages; // key = QQ,second = 用户信息

private:
    void ensureUserExistsUnlock(const uint64_t user_id);
    Person createDefaultPerson(const uint64_t user_id);
    const ModelRegistry &registry;
    BotConfig botConfig;
    ChatConfig chatConfig;
    ConversationStore &store;
    MemoryService *memoryService = nullptr;
    ImageAssetStore *imageAssetStore = nullptr;
};

#endif // USERSESSIONSERVICE_H

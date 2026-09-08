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
#include "../Application/BotIdentity.h"
#include "../ChatService/ChatOptions.h"
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
    // soulFile：默认人格文件，用户无持久化人格时读取；文件缺失/为空时
    // 退回内置默认 "你是<bot.name>，部署者的AI助手。"
    // personaSpecFile：人格编译规范（T5），缺失时禁用 AI 编译人格，直接用 soul.md
    UserSessionService(const ModelRegistry &mr, ConversationStore &store, const BotIdentity &bot,
                       const ChatOptions &chat, const std::string &soulFile = "source/soul.md",
                       const std::string &personaSpecFile = "source/persona-spec.md");
    void setMemoryService(MemoryService *service);
    void setImageAssetStore(ImageAssetStore *store);
    void ensureUserExists(const uint64_t user_id);
    // 轻重置（#重置对话）：清空内存镜像并把上下文起点落库，
    // SQLite 原始历史、长期记忆和图片资源保留，旧话题仍可召回
    void resetChat(const uint64_t user_id);
    // 彻底重置（#重置上下文）：内存、SQLite 原始历史、长期记忆和图片资源全删
    void resetContext(const uint64_t user_id);
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

    // ---- 人格编译（T5）：#重置对话 后首次聊天把 soul.md 编译为标签式 prompt ----
    // 是否存在待编译请求：手动人格存在或规范文件缺失时为 false
    bool personaBuildPending(const uint64_t user_id);
    // 编译素材（规范文件 + 当前 soul 内容）；不可用返回 false
    bool loadPersonaBuildMaterials(std::string &specOut, std::string &soulOut);
    // 应用编译产物：仅内存，不落 user_persona；空串视为失败、保留待编译标志
    void applyGeneratedPersona(const uint64_t user_id, const std::string &compiledPrompt);

private:
    std::mutex mutex_message;
    std::string default_personality;
    std::unique_ptr<std::unordered_map<uint64_t, Person>> user_messages; // key = QQ,second = 用户信息

private:
    void ensureUserExistsUnlock(const uint64_t user_id);
    Person createDefaultPerson(const uint64_t user_id);
    // 读取 soul.md 作为默认人格；不可用时退回 default_personality
    std::string loadSoulFallback();
    const ModelRegistry &registry;
    BotIdentity botIdentity;
    ChatOptions chatOptions;
    std::string soul_file;
    std::string persona_spec_file;
    ConversationStore &store;
    MemoryService *memoryService = nullptr;
    ImageAssetStore *imageAssetStore = nullptr;
};

#endif // USERSESSIONSERVICE_H

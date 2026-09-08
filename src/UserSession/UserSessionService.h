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
    // 人格编译规范内嵌于实现（D14：内部逻辑归代码），无对应文件
    UserSessionService(const ModelRegistry &mr, ConversationStore &store, const BotIdentity &bot,
                       const ChatOptions &chat, const std::string &soulFile = "source/soul.md");
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
    // 编译任务文本：system（内嵌规范）与 user（soul 包装）——规范内嵌恒可用，无失败路径
    void personaBuildTask(std::string &systemOut, std::string &userOut);
    // 应用编译产物：仅内存，不落 user_persona；空串视为失败、保留待编译标志
    void applyGeneratedPersona(const uint64_t user_id, const std::string &compiledPrompt);
    // 单飞编译者的完成入口：释放单飞位；成功则产物进共享缓存（哈希来自获权时快照）
    void finishPersonaBuild(const uint64_t user_id, const std::string &compiledPrompt);

    // ---- 共享编译缓存（T5）：产物按 (persona-spec + soul) 内容哈希寻址，全用户共用 ----
    // 哈希命中 → 直接复用产物（新用户零 LLM 调用）；未命中 → 单飞编译（并发只放行一个，
    // 其余用户本轮 soul 兜底、标志保留，编译完成后下条消息走缓存）
    std::optional<std::string> freshSharedPersona();
    bool tryBeginPersonaBuild();

private:
    std::mutex mutex_message;
    std::string default_personality;
    std::unique_ptr<std::unordered_map<uint64_t, Person>> user_messages; // key = QQ,second = 用户信息

private:
    void ensureUserExistsUnlock(const uint64_t user_id);
    Person createDefaultPerson(const uint64_t user_id);
    // 读取 soul.md 作为默认人格；不可用时退回 default_personality
    std::string loadSoulFallback() const;
    const ModelRegistry &registry;
    BotIdentity botIdentity;
    ChatOptions chatOptions;
    std::string soul_file;
    // 共享编译缓存（mutex_message 保护）；哈希 = std::hash(规范常量 + soul 内容)，
    // 仅进程内变更检测用（缓存不持久化，无跨进程稳定性需求）
    std::size_t shared_persona_hash_ = 0;
    std::string shared_persona_prompt_;
    bool shared_persona_ready_ = false;
    bool persona_build_in_progress_ = false;
    std::size_t persona_build_hash_ = 0; // 单飞期间的待写入哈希
    std::size_t computePersonaHash() const;
    ConversationStore &store;
    MemoryService *memoryService = nullptr;
    ImageAssetStore *imageAssetStore = nullptr;
};

#endif // USERSESSIONSERVICE_H

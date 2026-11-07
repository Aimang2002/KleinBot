#ifndef USERSESSIONSERVICE_H
#define USERSESSIONSERVICE_H

#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>
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
    // 本次构造时锚点前移、被移出窗口的原文（"用户: …"/"<bot.name>: …"每行一条）；
    // 锚点未动时为空。由 ChatService 在锁外交杂务模型合并进滚动摘要
    std::string evicted_lines;
    // 构造时 Person::context_digest 的快照（可能为空）
    std::string digest_before;
};

class UserSessionService
{
public:
    // soulFile：默认人格文件，用户无持久化人格时读取；文件缺失/为空时
    // 退回内置默认 "你是<bot.name>，部署者的AI助手。"
    // soul.md 原文直接作为 system prompt（人格编译链路已于 2026-09-18 移除）
    UserSessionService(const ModelRegistry &mr, ConversationStore &store, const BotIdentity &bot,
                       const ChatOptions &chat, const std::string &soulFile = "source/soul.md");
    void setMemoryService(MemoryService *service);
    void setImageAssetStore(ImageAssetStore *store);
    void ensureUserExists(const uint64_t user_id);
    // 进程内首次接触检测：每个用户只返回一次 true（mutex_message 保护）。
    // 上下文对所有用户开放后，模型可从会话历史自行判断初次接触；
    // 与 hasChatHistory 搭配用于"真·新朋友第一句话"注记——重启后重新计数，
    // 老朋友借 hasChatHistory 排除，最坏情况是重置过上下文的老用户再被介绍一次，可接受
    bool takeFirstContact(uint64_t user_id);
    // 该用户是否已有会话历史（冷启动镜像含 SQLite 读回的部分）；
    // 供"新朋友第一句话"注记区分初次接触与重启后的老朋友
    bool hasChatHistory(uint64_t user_id);
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
    // 私聊上下文 token 水位（2026-11-08，取代 40/20 条）：窗口（含滚动摘要）累计超过
    // 高水位时，锚点一次性前移到"从该点到末尾 ≤ 低水位"的位置
    static constexpr std::size_t kContextHighTokens = 32000;
    static constexpr std::size_t kContextLowTokens = 16000;
    // trimWindow=false：只装配人格/超参数/当前窗口，不前移锚点、不产出 evicted_lines。
    // 供 replyInCharacter/requestInCharacter 这类自带 history 的调用——它们丢弃窗口，
    // 若在这里前移锚点，被移出的那段就永远进不了摘要
    std::optional<ChatCallBundle> buildChatRequest(const uint64_t &user_id, bool trimWindow = true);
    // 滚动摘要读写（持锁）；只在内存，重启即空
    void setContextDigest(uint64_t user_id, const std::string &digest);
    std::string getContextDigest(uint64_t user_id);

private:
    std::mutex mutex_message;
    std::string default_personality;
    std::unique_ptr<std::unordered_map<uint64_t, Person>> user_messages; // key = QQ,second = 用户信息
    std::unordered_set<uint64_t> first_contact_seen_;

private:
    void ensureUserExistsUnlock(const uint64_t user_id);
    Person createDefaultPerson(const uint64_t user_id);
    // 读取 soul.md 作为默认人格；不可用时退回 default_personality
    std::string loadSoulFallback() const;
    const ModelRegistry &registry;
    BotIdentity botIdentity;
    ChatOptions chatOptions;
    std::string soul_file;
    ConversationStore &store;
    MemoryService *memoryService = nullptr;
    ImageAssetStore *imageAssetStore = nullptr;
};

#endif // USERSESSIONSERVICE_H

#include "UserSessionService.h"
#include "../JsonParse/JsonParse.h"
#include "../Log/Log.h"
#include "../Persistence/ConversationStore.h"
#include "../Memory/MemoryService.h"
#include "../Asset/ImageAssetStore.h"

UserSessionService::UserSessionService(const ModelRegistry &mr, ConversationStore &store,
                                       const BotConfig &bot, const ChatConfig &chat)
    : registry(mr), botConfig(bot), chatConfig(chat),
      default_personality("You are my assistant, your name is " + bot.name),
      user_messages(std::make_unique<std::unordered_map<uint64_t, Person>>()),
      store(store)
{
}

void UserSessionService::setMemoryService(MemoryService *service)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->memoryService = service;
}

void UserSessionService::setImageAssetStore(ImageAssetStore *store)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->imageAssetStore = store;
}

Person UserSessionService::createDefaultPerson(const uint64_t user_id)
{
    Person person;
    person.system_prompt = this->default_personality;
    person.current_model = chatConfig.defaultModel;
    person.isOpenVoiceMode = false;
    person.temperature = chatConfig.temperature;
    person.frequency_penalty = chatConfig.frequencyPenalty;
    person.presence_penalty = chatConfig.presencePenalty;

    return person;
}

void UserSessionService::ensureUserExistsUnlock(const uint64_t user_id)
{
    // 找到用户
    if (user_messages->find(user_id) != user_messages->end())
        return;

    Person p = this->createDefaultPerson(user_id);
    p.user_chatHistory = this->store.loadAll(user_id); // 冷启动从 SQLite 读回
    this->user_messages->emplace(user_id, p);
}

void UserSessionService::ensureUserExists(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
}

void UserSessionService::resetChat(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    // 重置对话会删除之前的所有信息，但不包括人格信息
    user->second.user_chatHistory.clear();
    this->store.clearUser(user_id); // 同步清库
    if (this->memoryService != nullptr)
        this->memoryService->clearUser(user_id);
    if (this->imageAssetStore != nullptr)
        this->imageAssetStore->clearUser(user_id);
}

std::string UserSessionService::getModelName(uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto res = this->user_messages->find(user_id);
    return res->second.current_model;
}

void UserSessionService::setPersonality(const uint64_t user_id, const std::string &Personality)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    user->second.system_prompt = Personality;
}

void UserSessionService::resetPersonality(const uint64_t user_id)
{
    std::lock_guard<std::mutex> locker(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    user->second.system_prompt = this->default_personality;
}

void UserSessionService::switchModel(const uint64_t user_id, const std::string &newModel)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    this->user_messages->find(user_id)->second.current_model = newModel;
}

void UserSessionService::voiceSwitch(const uint64_t user_id, const bool tag)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    user->second.isOpenVoiceMode = tag;
}

bool UserSessionService::isVoiceMode(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    return this->user_messages->find(user_id)->second.isOpenVoiceMode;
}

std::string UserSessionService::removePreviousContext(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto &user_context = this->user_messages->find(user_id)->second.user_chatHistory;

    for (auto it = user_context.rbegin(); it != user_context.rend(); ++it)
    {
        if (it->role == "user")
        {
            // erase 区间 [it.base()-1, end) 的条数 = 要从库里删除的末尾行数
            const int removed = static_cast<int>(user_context.end() - (it.base() - 1));
            user_context.erase(it.base() - 1, user_context.end());
            const int64_t firstRemovedId = this->store.removeLast(user_id, removed);
            if (this->memoryService != nullptr && firstRemovedId > 0)
                this->memoryService->removeBySourceFrom(user_id, firstRemovedId);
            if (this->imageAssetStore != nullptr && firstRemovedId > 0)
                this->imageAssetStore->removeByConversationFrom(user_id, firstRemovedId);
            return "上条对话已被删除！";
        }
    }
    return "没有上下文！";
}

std::vector<TimestampedMessage> UserSessionService::getChatHistory(const uint64_t user_id)
{
    std::lock_guard<std::mutex> locker(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    return this->user_messages->find(user_id)->second.user_chatHistory;
}

void UserSessionService::updateChatHistory(const uint64_t user_id, const std::vector<TimestampedMessage> &history)
{
    std::lock_guard<std::mutex> locker(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    this->user_messages->find(user_id)->second.user_chatHistory = history;
}

int64_t UserSessionService::appendMessage(const uint64_t user_id, const std::string &role, const std::string &content)
{
    std::lock_guard<std::mutex> locker(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    const time_t ts = std::time(nullptr);
    // 锁内：先改内存，再写库，保证两者一致
    this->user_messages->find(user_id)->second.user_chatHistory.push_back({role, content, ts});
    return this->store.append(user_id, role, content, ts);
}

Person UserSessionService::getUserConfig(const uint64_t user_id)
{
    std::lock_guard<std::mutex> locker(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    return this->user_messages->find(user_id)->second;
}

std::optional<ChatCallBundle> UserSessionService::buildChatRequest(const uint64_t &user_id)
{
    std::lock_guard<std::mutex> locker(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    const Person &p = this->user_messages->find(user_id)->second;

    // 模型查找：找不到直接返回 nullopt，让上层报错
    const ChatModel *modelPtr = registry.find(p.current_model);
    if (modelPtr == nullptr)
    {
        LOG_ERROR("模型未注册：" + p.current_model);
        return std::nullopt;
    }

    ChatCallBundle result;
    result.model = *modelPtr;
    result.model_name = p.current_model;

    // 超参数 + system_prompt 直接拷贝
    result.request.system_prompt = p.system_prompt;
    result.request.temperature = p.temperature;
    result.request.frequency_penalty = p.frequency_penalty;
    result.request.presence_penalty = p.presence_penalty;

    // ===== 临时裁切算法（占位，待 Phase 3 后期替换） =====
    // 策略：
    //   1. 丢弃超过存活时间的旧消息
    //   2. 若剩余条数超过 MAX_TURNS，只保留末尾若干条
    // 注意：本函数只读，不回写 user_chatHistory，原始历史保持完整
    const time_t now = std::time(nullptr);
    const time_t survival = chatConfig.messageSurvivalSeconds;
    const size_t MAX_TURNS = 20;

    std::vector<ChatMessage> filtered;
    filtered.reserve(p.user_chatHistory.size());
    for (const auto &tm : p.user_chatHistory)
    {
        if (tm.timestamp + survival < now)
            continue;
        filtered.push_back({tm.role, tm.content});
    }
    if (filtered.size() > MAX_TURNS)
    {
        filtered.erase(filtered.begin(), filtered.end() - MAX_TURNS);
    }
    result.request.history = std::move(filtered);

    return result;
}

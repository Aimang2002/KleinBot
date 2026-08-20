#include "UserSessionService.h"
#include "../JsonParse/JsonParse.h"
#include "../Log/Log.h"
#include "../Persistence/ConversationStore.h"
#include "../Memory/MemoryService.h"
#include "../Asset/ImageAssetStore.h"

#include <algorithm>

UserSessionService::UserSessionService(const ModelRegistry &mr, ConversationStore &store,
                                       const BotIdentity &bot, const ChatOptions &chat)
    : registry(mr), botIdentity(bot), chatOptions(chat),
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
    person.current_model = chatOptions.defaultModel;
    person.isOpenVoiceMode = false;
    person.temperature = chatOptions.temperature;
    person.frequency_penalty = chatOptions.frequencyPenalty;
    person.presence_penalty = chatOptions.presencePenalty;

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
    Person &p = this->user_messages->find(user_id)->second;

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
    //   1. 丢弃超过存活时间的旧消息（仅在缓存已冷的场景生效，
    //      供应商缓存 TTL 只有几分钟，隔天返回时缓存本来就失效）
    //   2. 高低水位裁切：history_anchor 记录当前窗口在存活消息列表中的
    //      起始下标，窗口长度未超过高水位时锚点不动，历史头部逐字节
    //      稳定，供应商前缀缓存才能跨请求命中；只有窗口实际越过高水位
    //      时才把锚点一次性前移到低水位边界，在截断那一轮付一次全量
    //      缓存 miss，随后前缀重新稳定。锚点必须持久在 Person 里：
    //      若按"存活总数是否超限"逐次判断，完整历史只增不减，首次
    //      截断后每一轮都会重新触发截断，退化为逐轮滑动的滑窗
    // 注意：裁切不回写 user_chatHistory，原始历史保持完整
    const time_t now = std::time(nullptr);
    const time_t survival = chatOptions.messageSurvivalSeconds;
    const size_t HISTORY_HIGH_WATERMARK = 40;
    const size_t HISTORY_LOW_WATERMARK = 20;

    std::vector<ChatMessage> filtered;
    filtered.reserve(p.user_chatHistory.size());
    for (const auto &tm : p.user_chatHistory)
    {
        if (tm.timestamp + survival < now)
            continue;
        filtered.push_back({tm.role, tm.content});
    }
    // 先夹紧锚点：撤回、重置或过期收缩存活列表时保证下标合法
    p.history_anchor = std::min(p.history_anchor, filtered.size());
    if (filtered.size() - p.history_anchor > HISTORY_HIGH_WATERMARK)
        p.history_anchor = filtered.size() - HISTORY_LOW_WATERMARK;
    result.request.history.assign(filtered.begin() + p.history_anchor,
                                  filtered.end());

    return result;
}

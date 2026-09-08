#include "UserSessionService.h"
#include "../JsonParse/JsonParse.h"
#include "../Log/Log.h"
#include "../Persistence/ConversationStore.h"
#include "../Memory/MemoryService.h"
#include "../Asset/ImageAssetStore.h"
#include "../utils/Utils.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

namespace
{
// 服务契约（D13）：助手优先、人格其次。固定追加在人格之后——职责归代码，
// 人格归 soul.md/#设置人格；措辞与具体角色解耦，自定义人格同样被契约包裹
const char *const kServiceContractFrame =
    "\n\n[服务契约，优先级高于以上人格] 你首先是部署者的助手："
    "对方的消息需要专业知识、事实检索或任务执行时，严谨、准确、简短，"
    "优先调用工具获取证据，不夹带人格化寒暄；"
    "对方在闲聊、倾诉或玩闹时，你就是以上人格所定义的角色，按其方式自由表达。"
    "判断依据只有一个：对方这条消息需要什么。";

// 人格编译规范（D14）：内部编译逻辑，内嵌为代码（随二进制版本化），
// 不是部署者可编辑内容——部署者只写 soul.md 源码。哈希输入包含本常量，
// 二进制升级若规范变化会自动令旧编译产物失效
const char *const kPersonaCompileSpec =
    "# 人格编译规范\n"
    "把给定的人格源描述编译为标签压缩式 system prompt。产物将由代码侧追加"
    "服务契约（助手优先、任务优先），因此产物中禁止出现任何职责条款、任务优先级"
    "声明或助手行为规则——那是契约的事，写了反而重复稀释。\n"
    "\n"
    "## 标签语法\n"
    "- 一行一个标签：CATEGORY_DESCRIPTOR，全大写 + 下划线。\n"
    "- 用 # 分组名 注释行分组，组顺序固定：角色身份 → 性格底色 → 工作风格 → 感性流露 → 行为规则。\n"
    "- 标签是激活词而非描述：一个标签对应语料中一个成熟的行为簇，"
    "如 PERSONALITY_TSUNDERE_SWEET 一个词顶十句描写。宁缺毋滥：每个标签必须能在"
    "源描述里找到依据，凑数的标签是噪声。\n"
    "\n"
    "## 编译规则\n"
    "1. 任务优先：行为规则组必须包含 RULE_NO_PURE_ROLEPLAY（不把真实问题演成剧情）"
    "与 RULE_ACCEPT_REAL_WORLD_QUERY（接受真实世界检索与追问）——角色是助手的外衣，不是剧本。\n"
    "2. 性格入标签不做叙事：不要外貌、背景故事和你是谁句式；每组 2~5 个标签。\n"
    "3. 感性流露单独分组且量少（1~3 个标签）：感性是点缀。\n"
    "4. 标签保持英文；回复语言随用户。\n"
    "5. 产物只含标签块：不要解释、不要代码围栏、不要标题。\n"
    "6. 禁止套用既有角色：产物只允许由本次输入的人格源描述推导，"
    "不得引入源描述中没有的既有角色名、设定或流行标签组合。\n";
}

UserSessionService::UserSessionService(const ModelRegistry &mr, ConversationStore &store,
                                       const BotIdentity &bot, const ChatOptions &chat,
                                       const std::string &soulFile)
    : registry(mr), botIdentity(bot), chatOptions(chat),
      default_personality("你是" + bot.name + "，部署者的AI助手。"),
      user_messages(std::make_unique<std::unordered_map<uint64_t, Person>>()),
      soul_file(soulFile), store(store)
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
    // 优先恢复持久化的人格（#设置人格），没有记录再回退 soul.md 默认人格
    person.system_prompt = this->store.loadPersona(user_id);
    if (person.system_prompt.empty())
        person.system_prompt = this->loadSoulFallback();
    // 每个新对话周期（冷启动/#重置对话）都重新编译人格（D14）；
    // 手动人格存在时 personaBuildPending 会自行排除
    person.persona_needs_build = true;
    person.current_model = chatOptions.defaultModel;
    person.isOpenVoiceMode = false;
    person.temperature = chatOptions.temperature;
    person.frequency_penalty = chatOptions.frequencyPenalty;
    person.presence_penalty = chatOptions.presencePenalty;

    return person;
}

std::string UserSessionService::loadSoulFallback() const
{
    std::ifstream input(this->soul_file);
    if (!input.is_open())
    {
        LOG_WARNING("默认人格文件缺失：" + this->soul_file + "，使用内置默认人格");
        return this->default_personality;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string soul = utils::trim(buffer.str());
    if (soul.empty())
    {
        LOG_WARNING("默认人格文件为空：" + this->soul_file + "，使用内置默认人格");
        return this->default_personality;
    }
    return soul;
}

void UserSessionService::ensureUserExistsUnlock(const uint64_t user_id)
{
    // 找到用户
    if (user_messages->find(user_id) != user_messages->end())
        return;

    Person p = this->createDefaultPerson(user_id);
    // 冷启动只读回上下文起点之后的消息：轻重置留下的旧话题行
    // 留在库里供召回，但不再进入内存镜像和模型窗口
    p.user_chatHistory = this->store.loadFrom(user_id, this->store.contextStartId(user_id));
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
    auto &history = user->second.user_chatHistory;
    // 轻重置：只丢弃内存镜像，并把起点持久化为最后一条消息的下一个 id，
    // 重启后冷启动不再读回旧话题；镜像清空后与库重新构成"后缀"关系，
    // #删除上条对话 的按条数删末尾逻辑因此天然只作用于新话题
    if (!history.empty())
        this->store.setContextStartId(user_id, history.back().id + 1);
    history.clear();
    user->second.history_anchor = 0;
    // 新话题配新人格：重置后首次聊天由 AI 按 persona-spec 重新编译
    // soul.md 为标签式 prompt（长期记忆不受影响——它以对话轮次为源，
    // 与 system_prompt 正交）
    user->second.persona_needs_build = true;
}

void UserSessionService::resetContext(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    // 彻底重置会删除之前的所有信息，但不包括人格信息
    user->second.user_chatHistory.clear();
    user->second.history_anchor = 0;
    this->store.clearUser(user_id); // 同步清库
    this->store.setContextStartId(user_id, 0);
    if (this->memoryService != nullptr)
        this->memoryService->clearUser(user_id);
    if (this->imageAssetStore != nullptr)
        this->imageAssetStore->clearUser(user_id);
    // 人格同样回归源码（D14）：清掉内存中的编译产物，回退到手动人格/soul.md，
    // 下轮聊天按 persona-spec + soul 重新编译。手动人格保留——
    // personaBuildPending 会排除，此处恢复的 system_prompt 即手动人格
    user->second.system_prompt = this->store.loadPersona(user_id);
    if (user->second.system_prompt.empty())
        user->second.system_prompt = this->loadSoulFallback();
    user->second.persona_needs_build = true;
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
    this->store.savePersona(user_id, Personality);
}

void UserSessionService::resetPersonality(const uint64_t user_id)
{
    std::lock_guard<std::mutex> locker(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    this->store.clearPersona(user_id);
    // 每次重读 soul.md：管理员改默认人格后，#人格还原 无需重启即可生效
    user->second.system_prompt = this->loadSoulFallback();
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
    // 锁内：先改内存，再写库，保证两者一致；行 id 回填镜像，
    // 供 resetChat 计算上下文起点边界
    auto &history = this->user_messages->find(user_id)->second.user_chatHistory;
    history.push_back({role, content, ts});
    const int64_t id = this->store.append(user_id, role, content, ts);
    history.back().id = id;
    return id;
}

Person UserSessionService::getUserConfig(const uint64_t user_id)
{
    std::lock_guard<std::mutex> locker(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    return this->user_messages->find(user_id)->second;
}

bool UserSessionService::personaBuildPending(const uint64_t user_id)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    // 手动人格（#设置人格）是部署者的明确意志，永远优先，不编译
    if (!this->store.loadPersona(user_id).empty())
        return false;
    return user->second.persona_needs_build;
}

void UserSessionService::applyGeneratedPersona(const uint64_t user_id,
                                               const std::string &compiledPrompt)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    // 共享缓存命中的纯应用：不改共享缓存状态、不碰单飞位
    user->second.system_prompt = compiledPrompt;
    user->second.persona_needs_build = false;
}

void UserSessionService::finishPersonaBuild(const uint64_t user_id,
                                            const std::string &compiledPrompt)
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    auto user = this->user_messages->find(user_id);
    // 无论成败都释放单飞位（调用方必是获权线程）
    this->persona_build_in_progress_ = false;

    if (compiledPrompt.empty())
    {
        // 编译失败：保留待编译标志，下一条聊天消息由其他人/自己重试
        LOG_WARNING("人格编译产物为空，保留重试标志");
        return;
    }
    user->second.system_prompt = compiledPrompt;
    user->second.persona_needs_build = false;
    // 发布进共享缓存：同哈希的后续用户（含并发的迟到者）零 LLM 复用
    this->shared_persona_prompt_ = compiledPrompt;
    this->shared_persona_hash_ = this->persona_build_hash_;
    this->shared_persona_ready_ = true;
    LOG_INFO("人格编译完成并已共享，长度：" + std::to_string(compiledPrompt.size()));
}

std::size_t UserSessionService::computePersonaHash() const
{
    // 变更检测哈希：规范常量（进程内恒定）+ soul 内容。std::hash 足够——
    // 缓存不持久化、无跨进程稳定性需求，部署者自有文件也无对抗性输入
    return std::hash<std::string>{}(std::string(kPersonaCompileSpec) + "\x1f" +
                                    this->loadSoulFallback());
}

std::optional<std::string> UserSessionService::freshSharedPersona()
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    if (!this->shared_persona_ready_)
        return std::nullopt;
    if (this->computePersonaHash() != this->shared_persona_hash_)
        return std::nullopt; // soul 变过：产物过期，需要重编
    return this->shared_persona_prompt_;
}

bool UserSessionService::tryBeginPersonaBuild()
{
    std::lock_guard<std::mutex> lock(this->mutex_message);
    if (this->persona_build_in_progress_)
        return false; // 单飞：别的线程正在编译，调用方本轮 soul 兜底
    this->persona_build_in_progress_ = true;
    this->persona_build_hash_ = this->computePersonaHash();
    return true;
}

void UserSessionService::personaBuildTask(std::string &systemOut, std::string &userOut)
{
    systemOut = kPersonaCompileSpec;
    userOut = "以下是人格源描述（soul.md）：\n" + this->loadSoulFallback() +
              "\n\n请按规范把它编译为标签块，只输出标签块本身。";
}

std::optional<ChatCallBundle> UserSessionService::buildChatRequest(const uint64_t &user_id)
{
    std::lock_guard<std::mutex> locker(this->mutex_message);
    this->ensureUserExistsUnlock(user_id);
    Person &p = this->user_messages->find(user_id)->second;

    // 模型查找：找不到直接返回 nullopt，让上层报错
    std::optional<ChatModel> model = registry.find(p.current_model);
    if (!model)
    {
        LOG_ERROR("模型未注册：" + p.current_model);
        return std::nullopt;
    }

    ChatCallBundle result;
    result.model = std::move(*model);
    result.model_name = p.current_model;

    // 超参数 + system_prompt：人格 + 服务契约（契约总是包裹在人格外层，含自定义人格）
    result.request.system_prompt = p.system_prompt + kServiceContractFrame;
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

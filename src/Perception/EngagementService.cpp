#include "EngagementService.h"
#include "../Log/Log.h"
#include "../Tool/ToolArgumentParser.h"
#include "../utils/Utils.hpp"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace
{
// 状态机常量（真机再调的初值，零旋钮）
constexpr std::size_t kJudgeWindowMessages = 6;  // judge 迷你窗口：触发句+前后文
constexpr std::size_t kTurnMaterialMessages = 30; // 轮次材料的原文窗口条数
constexpr int kMaxTurnsPerSession = 20;          // 单会话轮次硬上限
constexpr std::int64_t kMaxSessionSeconds = 30 * 60;  // 单会话时长硬上限
constexpr std::int64_t kInactiveSeconds = 10 * 60;    // 群内无新消息收场
constexpr int kPassLimit = 3;                    // 连续 pass 收场
constexpr std::int64_t kLullSeconds = 10;        // 新消息后的静默判定
constexpr int kNewMessagesForTurn = 3;           // lull 轮次的新消息门槛
constexpr std::int64_t kTurnCooldown = 45;       // 跨轮次节流
constexpr std::int64_t kJudgeMinInterval = 60;   // judge 每群最小间隔
constexpr std::size_t kJudgeHourlyCap = 6;       // judge 每群每小时上限
constexpr std::int64_t kJudgeFuseWindow = 60 * 60;
constexpr std::int64_t kRetentionSeconds = 24 * 60 * 60; // 结束后保留期

// 会话轮契约：动作三选一，行为约束归 system（静态文本保证前缀缓存稳定）
const char *const kEngagementContract =
    "\n\n[系统注] 你正在QQ群里跟进一个话题，你的每轮输出是一次群插话，不是一对一对话："
    "简短、口语、自然，不 @ 任何人，不假设自己拥有最后一句话。每轮动作三选一："
    "①要说话就调用 group_say，text 就是那句话本身（原样发出，不要引号、前缀或解释）；"
    "②这一拍没有增量、时机不对或还没轮到你开口：不调用任何工具、不输出任何文字；"
    "③跟进该结束（话题已翻页/无话可说/明显没人接你的话）就调用 leave_topic："
    "reason 写一句话理由（只进日志，不会发到群里），final_text 是离场前想留下的最后一句（可为空）。"
    "绝不直接用文字回复——文字内容只有经 group_say 才会被发出。"
    "别人直接点名你、问你的还没被回答的问题，不许沉默。";

const char *const kEntryAsk =
    "\n\n以上是这个群话题的现场记录。你刚决定介入这个对话：值得加入就用一句自然的"
    "话开口（像一直在群里潜水的人说话）；看完材料发现不适合介入就 leave_topic。";

const char *const kFollowUpAsk =
    "\n\n以上是你在跟的话题的最新进展。判断此刻该不该说话：有增量就 group_say，"
    "没有就保持沉默，话题明显翻页就 leave_topic。";

const char *const kAddressAsk =
    "\n\n以上是群聊现场。有人点名你或回复你，必须回应：group_say 或离场前说明。";

const char *const kSaySchema =
    R"({"type":"function","function":{"name":"group_say","description":"在群里说一句话（原样发出）。要说话时调用，绝不直接用文字回复。","parameters":{"type":"object","properties":{"text":{"type":"string","description":"要说的那句话本身，口语、自然、简短"}},"required":["text"]}}})";

const char *const kLeaveSchema =
    R"({"type":"function","function":{"name":"leave_topic","description":"结束这次话题跟进。话题翻页、无话可说或不该继续跟时调用。","parameters":{"type":"object","properties":{"reason":{"type":"string","description":"一句话离场理由（仅日志）"},"final_text":{"type":"string","description":"离场前想留下的最后一句话，可为空"}},"required":["reason"]}}})";

const char *const kJudgeSystem =
    "你是判定器。QQ群聊机器人 Klein 监控了一段群聊记录，最后一条消息提到了"
    "「Klein」这个名字但没有 @ 他。判断 Klein 是否应该介入这段对话。"
    "只输出一行：介入输出 YES；不介入输出 NO 加冒号和一句话理由。"
    "倾向：明显在对 Klein 说话、提问或呼唤 → YES；"
    "只是在聊别的话题时碰巧含这个词、玩梗、或刷屏 → NO。";

std::string formatClock(std::int64_t ts)
{
    const std::time_t seconds = static_cast<std::time_t>(ts);
    std::tm *local = std::localtime(&seconds);
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d", local->tm_hour, local->tm_min);
    return buffer;
}

std::string jsonArg(const nlohmann::json &arguments, const char *key)
{
    if (arguments.is_object() && arguments.contains(key) && arguments[key].is_string())
        return arguments[key].get<std::string>();
    return {};
}
} // namespace

EngagementService::EngagementService(BotIdentity bot, GroupContextStore *store,
                                     MessageSenderPort *sender,
                                     std::function<std::int64_t()> clock)
    : bot(bot), store(store), sender(sender),
      clock(clock ? std::move(clock)
                  : [] { return static_cast<std::int64_t>(std::time(nullptr)); })
{
}

void EngagementService::onGroupMessage(const GroupMessageRecord &record, bool atMentioned)
{
    const std::int64_t now = record.timestamp;
    std::lock_guard<std::mutex> lock(mutex);

    auto it = sessions.find(record.groupId);
    if (it == sessions.end() || it->second.state != EngagementState::Active)
    {
        // 无存活会话：提名字无 @ 是软激活信号，合流提交判定
        if (!atMentioned && record.mentionsBot && !record.isSelf &&
            pendingJudge[record.groupId] != true && submitJudge)
        {
            pendingJudge[record.groupId] = true;
            if (allowJudgeLocked(record.groupId, now))
            {
                LOG_INFO("话题介入判定提交：群 " + std::to_string(record.groupId));
                submitJudge(record.groupId);
            }
            else
            {
                pendingJudge[record.groupId] = false;
            }
        }
        return;
    }

    EngagementSession &session = it->second;
    session.lastActivityTs = now;
    session.newMessagesSinceTurn += 1;

    // 点名（@ 或提名字）或引用回复她：待回应，立即触发一轮
    if (!record.isSelf &&
        (atMentioned || record.mentionsBot || replyToSelf(record)))
    {
        const bool alreadyPending = session.addressPending;
        session.addressPending = true;
        if (!alreadyPending && submitTurn)
            submitTurn(record.groupId);
    }
}

void EngagementService::onAtActivated(std::uint64_t groupId, const std::string &triggerText)
{
    const std::int64_t now = clock();
    std::lock_guard<std::mutex> lock(mutex);
    auto it = sessions.find(groupId);
    if (it != sessions.end() && it->second.state == EngagementState::Active)
    {
        it->second.lastActivityTs = now; // 会话已在跟：@ 只是活动
        return;
    }
    createSessionLocked(groupId, triggerText, now, "@");
}

void EngagementService::onSelfActivity(std::uint64_t groupId, std::int64_t now)
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = sessions.find(groupId);
    if (it == sessions.end() || it->second.state != EngagementState::Active)
        return;
    EngagementSession &session = it->second;
    session.lastActivityTs = now;
    session.lastTurnTs = now;
    session.consecutivePasses = 0;
    session.newMessagesSinceTurn = 0;
}

void EngagementService::pump(std::int64_t now)
{
    std::vector<std::uint64_t> dueTurns;
    {
        std::lock_guard<std::mutex> lock(mutex);
        for (auto it = sessions.begin(); it != sessions.end();)
        {
            EngagementSession &session = it->second;
            if (session.state == EngagementState::Ended)
            {
                // 保留期满：结束会话的上下文引用清扫（群内容库自按 24h TTL 淘汰）
                if (now - session.endTs >= kRetentionSeconds)
                {
                    LOG_INFO("会话保留期满清理：群 " + std::to_string(session.groupId) +
                             "（上一话题：" + session.topicLine + "）");
                    it = sessions.erase(it);
                    continue;
                }
                ++it;
                continue;
            }

            // 硬后盖：不依赖模型自觉
            if (session.turnCount >= kMaxTurnsPerSession)
            {
                endSessionLocked(session, now, "轮次达上限", true);
                ++it;
                continue;
            }
            if (now - session.startTs >= kMaxSessionSeconds)
            {
                endSessionLocked(session, now, "跟进时长达上限", true);
                ++it;
                continue;
            }
            if (now - session.lastActivityTs >= kInactiveSeconds)
            {
                endSessionLocked(session, now, "话题沉寂", false);
                ++it;
                continue;
            }

            // lull 轮次：攒够新消息 + 突发归于安静 + 节流
            if (session.newMessagesSinceTurn >= kNewMessagesForTurn &&
                now - session.lastActivityTs >= kLullSeconds &&
                now - session.lastTurnTs >= kTurnCooldown)
            {
                session.newMessagesSinceTurn = 0;
                dueTurns.push_back(session.groupId);
            }
            ++it;
        }
    }
    if (submitTurn != nullptr)
        for (std::uint64_t groupId : dueTurns)
            submitTurn(groupId);
}

void EngagementService::runJudge(std::uint64_t groupId)
{
    if (worker == nullptr)
    {
        std::lock_guard<std::mutex> lock(mutex);
        pendingJudge[groupId] = false;
        return;
    }
    std::vector<GroupMessageRecord> window;
    {
        std::lock_guard<std::mutex> lock(mutex);
        pendingJudge[groupId] = false;
        auto it = sessions.find(groupId);
        if (it != sessions.end() && it->second.state == EngagementState::Active)
            return; // 判定提交路上会话已被 @/其他路径开启
        if (store == nullptr)
            return;
        const auto records = store->snapshot(groupId);
        if (records.empty())
            return;
        window.assign(records.end() - std::min(records.size(), kJudgeWindowMessages),
                      records.end());
    }

    std::string material;
    for (std::size_t index = 0; index < window.size(); ++index)
    {
        const GroupMessageRecord &record = window[index];
        material += "[" + formatClock(record.timestamp) + "] " +
                    (record.isSelf ? bot.name + "（你）" : record.nickname) + ": " +
                    record.text;
        if (index + 1 == window.size())
            material += "　← 这句提到了你";
        material += "\n";
    }
    const std::string verdict = worker(kJudgeSystem, material);
    if (verdict.empty())
    {
        LOG_WARNING("话题介入判定失败（杂务模型无返回）：群 " + std::to_string(groupId));
        return;
    }
    const std::string trimmed = utils::trim(verdict);
    if (trimmed.rfind("YES", 0) != 0)
    {
        LOG_INFO("话题介入判定 NO：群 " + std::to_string(groupId) + "（" + trimmed + "）");
        return;
    }

    std::int64_t now = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        // 判定期间会话可能已被 @ 路径开启：已在跟则不再重复激活
        auto it = sessions.find(groupId);
        if (it != sessions.end() && it->second.state == EngagementState::Active)
            return;
        now = clock();
        createSessionLocked(groupId, window.back().text, now, "提名字");
        sessions[groupId].entryPending = true;
    }
    if (submitTurn != nullptr)
        submitTurn(groupId);
}

void EngagementService::runTurn(std::uint64_t groupId)
{
    if (mainAgent == nullptr || store == nullptr)
        return;

    enum class TurnKind
    {
        Entry,
        FollowUp,
        Address,
    };
    std::string material;
    TurnKind kind = TurnKind::FollowUp;
    std::int64_t startTs = 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto it = sessions.find(groupId);
        if (it == sessions.end() || it->second.state != EngagementState::Active)
            return;
        EngagementSession &session = it->second;

        // 锁内快照触发语境与硬后盖（模型调用在锁外，可能耗时数秒）
        if (session.turnCount >= kMaxTurnsPerSession ||
            clock() - session.startTs >= kMaxSessionSeconds)
        {
            endSessionLocked(session, clock(), "轮次/时长达上限", true);
            return;
        }
        kind = session.addressPending ? TurnKind::Address
                                      : (session.entryPending ? TurnKind::Entry
                                                              : TurnKind::FollowUp);
        startTs = session.startTs;

        const auto records = store->snapshot(groupId);
        if (!records.empty())
            material = buildMaterial(records, kTurnMaterialMessages);
        if (material.empty())
            return;
    }

    const char *ask = kFollowUpAsk;
    if (kind == TurnKind::Entry)
        ask = kEntryAsk;
    else if (kind == TurnKind::Address)
        ask = kAddressAsk;

    std::vector<ChatMessage> history;
    history.push_back({"user", material + ask});
    ChatResponse response = mainAgent(kEngagementContract, history, {kSaySchema, kLeaveSchema});
    if (response.cancelled || response.code != 200)
    {
        LOG_WARNING("会话轮调用失败（code=" + std::to_string(response.code) + "）：群 " +
                    std::to_string(groupId));
        return;
    }

    const EngagementTurn turn = parseTurn(response);
    const std::int64_t now = clock();
    std::lock_guard<std::mutex> lock(mutex);
    auto it = sessions.find(groupId);
    if (it == sessions.end() || it->second.state != EngagementState::Active ||
        it->second.startTs != startTs)
        return; // 模型调用期间会话已被结束或替换
    EngagementSession &session = it->second;
    session.addressPending = false;
    session.entryPending = false;
    session.lastTurnTs = now;
    session.newMessagesSinceTurn = 0;

    switch (turn.kind)
    {
    case EngagementTurn::Kind::Speak:
    {
        if (utils::trim(turn.text).empty())
        {
            // group_say 带了空文本：按沉默处理
            session.consecutivePasses += 1;
            if (session.consecutivePasses >= kPassLimit)
                endSessionLocked(session, now, "连续沉默", false);
            break;
        }
        deliverText(groupId, turn.text);
        recordEngagementSpeech(groupId, turn.text, now);
        session.turnCount += 1;
        session.consecutivePasses = 0;
        LOG_INFO("会话轮发言：群 " + std::to_string(groupId) + "（第 " +
                 std::to_string(session.turnCount) + " 轮）");
        break;
    }
    case EngagementTurn::Kind::Leave:
        LOG_INFO("会话离场：群 " + std::to_string(groupId) + "（" + turn.reason + "）");
        if (!utils::trim(turn.text).empty())
        {
            deliverText(groupId, turn.text);
            recordEngagementSpeech(groupId, turn.text, now);
        }
        endSessionLocked(session, now, turn.reason.empty() ? "模型离场" : turn.reason, false);
        break;
    case EngagementTurn::Kind::Pass:
    default:
        session.consecutivePasses += 1;
        if (session.consecutivePasses >= kPassLimit)
            endSessionLocked(session, now, "连续沉默", false);
        else
            LOG_INFO("会话轮沉默：群 " + std::to_string(groupId) + "（连续 " +
                     std::to_string(session.consecutivePasses) + " 拍）");
        break;
    }
}

EngagementTurn EngagementService::parseTurn(const ChatResponse &response)
{
    EngagementTurn turn;
    for (const auto &call : response.tool_calls)
    {
        if (call.name != "group_say" && call.name != "leave_topic")
            continue;
        const nlohmann::json arguments = parseToolArguments(call.arguments);
        if (call.name == "group_say")
        {
            turn.kind = EngagementTurn::Kind::Speak;
            turn.text = jsonArg(arguments, "text");
            turn.reason.clear();
            return turn;
        }
        turn.kind = EngagementTurn::Kind::Leave;
        turn.reason = jsonArg(arguments, "reason");
        turn.text = jsonArg(arguments, "final_text");
        return turn;
    }
    if (!response.tool_calls.empty())
        return turn; // 只有未知工具：视为本拍沉默
    turn.text = utils::trim(response.content);
    turn.kind = turn.text.empty() ? EngagementTurn::Kind::Pass : EngagementTurn::Kind::Speak;
    return turn;
}

std::optional<EngagementSession> EngagementService::sessionOf(std::uint64_t groupId) const
{
    std::lock_guard<std::mutex> lock(mutex);
    auto it = sessions.find(groupId);
    if (it == sessions.end())
        return std::nullopt;
    return it->second;
}

void EngagementService::createSessionLocked(std::uint64_t groupId, const std::string &topicLine,
                                            std::int64_t now, const char *source)
{
    auto it = sessions.find(groupId);
    if (it != sessions.end())
    {
        LOG_INFO("旧会话上下文让位：群 " + std::to_string(groupId) + "（上一话题：" +
                 it->second.topicLine + "）");
        sessions.erase(it);
    }
    EngagementSession session;
    session.groupId = groupId;
    session.topicLine = topicLine;
    session.startTs = now;
    session.lastActivityTs = now;
    session.lastTurnTs = now; // 开场不计 lull，节流从激活时刻起算
    sessions[groupId] = session;
    LOG_INFO("话题会话开启（" + std::string(source) + "）：群 " + std::to_string(groupId) +
             "（话题：" + topicLine + "）");
}

void EngagementService::endSessionLocked(EngagementSession &session, std::int64_t now,
                                         std::string reason, bool forced)
{
    session.state = EngagementState::Ended;
    session.endTs = now;
    session.endReason = forced ? "forced: " + reason : reason;
    LOG_INFO("话题会话结束（" + std::string(forced ? "强制" : "自然") + "）：群 " +
             std::to_string(session.groupId) + "，原因：" + reason + "，保留 24 小时");
}

void EngagementService::deliverText(std::uint64_t groupId, const std::string &text)
{
    if (sender == nullptr || utils::trim(text).empty())
        return;
    OutboundDelivery delivery;
    delivery.target = GroupMessageTarget{std::to_string(groupId)};
    delivery.message = TextMessage{utils::trim(text)};
    sender->deliver(std::move(delivery));
}

void EngagementService::recordEngagementSpeech(std::uint64_t groupId, const std::string &text,
                                               std::int64_t now)
{
    if (store == nullptr)
        return;
    GroupMessageRecord record;
    record.groupId = groupId;
    record.userId = bot.id;
    record.nickname = bot.name;
    record.text = text;
    record.timestamp = now;
    record.isSelf = true;
    store->append(record);
}

bool EngagementService::replyToSelf(const GroupMessageRecord &record) const
{
    if (record.replyToMessageId.empty() || store == nullptr)
        return false;
    for (const GroupMessageRecord &candidate : store->snapshot(record.groupId))
    {
        if (candidate.isSelf && !candidate.messageId.empty() &&
            candidate.messageId == record.replyToMessageId)
            return true;
    }
    return false;
}

bool EngagementService::allowJudgeLocked(std::uint64_t groupId, std::int64_t now)
{
    auto &history = judgeHistory[groupId];
    if (!history.empty() && now - history.back() < kJudgeMinInterval)
        return false;
    while (!history.empty() && now - history.front() >= kJudgeFuseWindow)
        history.pop_front();
    if (history.size() >= kJudgeHourlyCap)
        return false;
    history.push_back(now);
    return true;
}

std::string EngagementService::buildMaterial(const std::vector<GroupMessageRecord> &records,
                                             std::size_t maxMessages) const
{
    const std::size_t taken = std::min(records.size(), maxMessages);
    std::string material;
    for (auto it = records.end() - static_cast<long>(taken); it != records.end(); ++it)
    {
        material += "[" + formatClock(it->timestamp) + "] " +
                    (it->isSelf ? bot.name + "（你）" : it->nickname) + ": " + it->text + "\n";
    }
    return material;
}

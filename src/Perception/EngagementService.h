#ifndef ENGAGEMENT_SERVICE_H
#define ENGAGEMENT_SERVICE_H

/*
 * 群聊话题会话（意志层 v2）：每群至多一个存活会话，一次话题跟进的完整
 * 生命周期——激活 → 轮次循环 → 结束保留 24h → 清扫/被新会话覆盖。
 *
 * 激活两条路：@ 是硬激活（必须回答走既有回复路径，这里只开会话）；
 * 提名字无 @ 是软激活——worker 模型 judge 看迷你窗口判定是否介入
 * （保险丝：每群 60s 最小间隔 + 每小时上限），判定通过才开会话并出
 * 入场轮。会话存活期间，新的点名/提及一律并入本会话，不再重复激活。
 *
 * 轮次循环：主 Agent（人格 system + 公共上下文材料 + 动作工具表）每轮
 * 三选一——group_say（说话）/ 不调用工具且无文字（pass，连续 3 拍收场）/
 * leave_topic（离场，可带最后一句）。硬后盖不依赖模型自觉：轮次上限、
 * 会话时长、群内不活跃时长任一触顶强制结束。
 *
 * 结束不是删除：会话（含话题行）保留 24 小时，期间 judge 材料可引用
 * "上次话题"；保留期内同群新激活 = 清旧建新（旧上下文让位），24h 无
 * 续接由 pump 清扫。进程重启即失（短时状态，群内容库才是持久层）。
 *
 * 线程模型：onGroupMessage/onAtActivated/onSelfActivity 在 worker 线程，
 * pump 在 pollingThread，runTurn/runJudge 经 submit seam 在群 lane worker
 * 执行——内部 mutex 统一保护，模型调用一律在锁外。
 */
#include "../Application/BotIdentity.h"
#include "GroupContextStore.h"
#include "../../Library/nlohmann/json.hpp"
#include "../Port/ChatRequest.h"
#include "../Port/ChatResponse.h"
#include "../Port/MessageSenderPort.h"

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

enum class EngagementState
{
    Active, // 存活：接受消息喂食、可触发轮次
    Ended,  // 结束：保留 24h 供续接判断，之后清扫
};

struct EngagementSession
{
    EngagementState state = EngagementState::Active;
    std::uint64_t groupId = 0;
    std::string topicLine;           // 话题一句话（激活时=触发句，可被轮次刷新）
    std::int64_t startTs = 0;
    std::int64_t lastActivityTs = 0; // 群内最近新消息（不活跃收场的基准）
    std::int64_t lastTurnTs = 0;     // 她最近一轮（节流基准）
    std::int64_t endTs = 0;          // Ended 生效时刻（保留期起算）
    int turnCount = 0;
    int consecutivePasses = 0;
    int newMessagesSinceTurn = 0;    // lull 轮次触发的新消息门槛
    int recallUsed = 0;              // 上下文召回预算（每会话 1 次）
    std::string digest;              // 已压缩部分的滚动摘要
    std::int64_t compressedUpToTs = 0; // 摘要覆盖到的时刻（之后为原文窗口）
    bool addressPending = false;     // 被点名/被回复，待回应（优先触发）
    bool entryPending = false;       // 软激活后的入场轮待执行（@ 激活无入场轮）
    bool coldPending = false;        // 冷启动入场轮待执行（热度尖峰自决）
    bool coldInitiated = false;      // 本会话由冷启动开启（结束冷却更久）
    std::string endReason;
};

// 主 Agent 单轮的解析结果（say / pass / leave 三选一）
struct EngagementTurn
{
    enum class Kind
    {
        Speak,
        Pass,
        Leave,
    };
    Kind kind = Kind::Pass;
    std::string text;   // Speak：那句话；Leave：final_text（可空）
    std::string reason; // Leave：离场理由（仅日志）
};

class EngagementService
{
public:
    // 主模型会话轮 seam（组合根适配 ChatService::requestInCharacter——人格
    // system + 调用方 history/tools，单次请求不落库；测试注入 fake）
    using MainAgent = std::function<ChatResponse(const std::string &systemNote,
                                                 const std::vector<ChatMessage> &history,
                                                 const std::vector<std::string> &toolSchemas)>;
    // 杂务模型 seam（组合根适配 ChatService::buildOnceWith——激活 judge、
    // 上下文压缩等后台苦力任务；空/失败返回空串）
    using Worker = std::function<std::string(const std::string &systemPrompt,
                                             const std::string &userPrompt)>;
    // 任务提交 seam（组合根适配 KeyedTaskScheduler::submit，按群占 lane；
    // 同群串行、不阻塞 worker/polling 线程；测试注入同步执行器）
    using Submit = std::function<void(std::uint64_t groupId)>;
    // 群人数查询 seam（组合根适配 GroupListService 镜像；冷启动阈值的人数
    // 地板/先验用；查不到返回 0）。测试注入常量
    using MemberProvider = std::function<long(std::uint64_t groupId)>;

    EngagementService(BotIdentity bot, GroupContextStore *store,
                      MessageSenderPort *sender = nullptr,
                      std::function<std::int64_t()> clock = {});

    void setMainAgent(MainAgent mainAgent) { this->mainAgent = std::move(mainAgent); }
    void setWorker(Worker worker) { this->worker = std::move(worker); }
    void setSubmitTurn(Submit submitTurn) { this->submitTurn = std::move(submitTurn); }
    void setSubmitJudge(Submit submitJudge) { this->submitJudge = std::move(submitJudge); }
    void setMemberProvider(MemberProvider memberProvider)
    {
        this->memberProvider = std::move(memberProvider);
    }

    // worker 线程：监控群新消息（record 已入库）。会话存活期喂活动/点名；
    // 无存活会话时提名字无 @ 是软激活信号（judge 保险丝后提交判定）
    void onGroupMessage(const GroupMessageRecord &record, bool atMentioned);

    // @ 硬激活（Message 群 @ 路径调用）：清旧建新开会话；回答本身走既有
    // 回复路径，不入场轮。会话已存活时仅记活动
    void onAtActivated(std::uint64_t groupId, const std::string &triggerText);

    // 她经正常路径发言（@ 回复等）：算一轮会话活动
    void onSelfActivity(std::uint64_t groupId, std::int64_t now);

    // pollingThread 每 3s：硬后盖（轮次/时长/不活跃）+ 保留期清扫 + lull 轮次触发
    void pump(std::int64_t now);

    // 群 lane worker 内执行：软激活判定（judge YES → 清旧建新 + 入场轮）
    void runJudge(std::uint64_t groupId);

    // 群 lane worker 内执行：一轮跟进（装配材料 → 主 Agent → 动作分发）
    void runTurn(std::uint64_t groupId);

    // 轮次动作解析：工具调用 group_say/leave_topic 定案；无工具时非空文字
    // 宽容视为说话（模型偶尔不带工具直出），空文字 = pass
    static EngagementTurn parseTurn(const ChatResponse &response);

    // token 启发式估算（预算门控用，非精确计价）：CJK 码点 ≈1 token/字，
    // ASCII ≈1 token/4 字符
    static std::size_t estimateTokens(const std::string &text);

    // 调试/测试观测
    std::optional<EngagementSession> sessionOf(std::uint64_t groupId) const;

private:
    struct PreparedContext
    {
        std::string material;
        std::string digest;
        std::int64_t compressedUpToTs = 0;
        bool digestUpdated = false;
        std::vector<std::int64_t> shownSeqs; // 材料里出现过的消息（召回排除用）
    };

    void createSessionLocked(std::uint64_t groupId, const std::string &topicLine,
                             std::int64_t now, const char *source);
    void endSessionLocked(EngagementSession &session, std::int64_t now,
                          std::string reason, bool forced);
    void deliverText(std::uint64_t groupId, const std::string &text);
    void recordEngagementSpeech(std::uint64_t groupId, const std::string &text,
                                std::int64_t now);
    bool replyToSelf(const GroupMessageRecord &record) const;
    bool allowJudgeLocked(std::uint64_t groupId, std::int64_t now);
    // 材料装配：原文窗口（超长占位）+ 预算内直灌；超预算把最旧一段经
    // worker 合并进滚动摘要，只留原文尾巴。worker 调用在锁外由调用方保证
    PreparedContext prepareContext(const std::vector<GroupMessageRecord> &records,
                                   const std::string &digest,
                                   std::int64_t compressedUpToTs) const;
    std::string runContextRecall(const nlohmann::json &arguments, std::uint64_t groupId,
                                 const std::vector<std::int64_t> &shownSeqs) const;
    // 冷启动每群状态（纯内存，重启即失）
    struct ColdState
    {
        std::deque<std::int64_t> recent; // 最近 10 分钟消息时间戳（尖峰检测）
        std::deque<std::int64_t> hour;   // 最近 1 小时消息时间戳（基线学习）
        double hourlyEma = -1.0;         // 每小时消息数的 EMA；-1 = 未学习
        std::int64_t emaTs = 0;          // 上次 EMA 采样时刻
        std::int64_t armedTs = 0;        // 首条消息时刻（武装期起算）
        std::int64_t lastCheck = 0;      // 上次冷启动检查（60s 节流）
        std::int64_t dayAnchor = 0;      // 日界锚点
        int coldToday = 0;               // 今日冷启动评估次数
    };

    // 冷启动：基线学习 + 热度尖峰检测 + 频控（锁内调用，pump 每 3s 触发、
    // 内部 60s 节流）；返回 true 表示命中尖峰、由调用方开冷启动会话
    bool coldCheckLocked(std::uint64_t groupId, ColdState &cold, std::int64_t now);
    // 学习/频控状态落库（UPSERT engagement_cold；锁内调用，低频）
    void persistColdLocked(std::uint64_t groupId);

    const BotIdentity bot;
    GroupContextStore *const store;
    MessageSenderPort *const sender;
    const std::function<std::int64_t()> clock;
    mutable std::mutex mutex;
    std::map<std::uint64_t, EngagementSession> sessions;
    std::map<std::uint64_t, ColdState> colds;
    std::map<std::uint64_t, std::int64_t> coldCooldownUntil; // 冷启动禁入截止
    std::map<std::uint64_t, bool> pendingJudge;  // judge 提交合流（防重复提交）
    std::map<std::uint64_t, std::deque<std::int64_t>> judgeHistory; // 保险丝记录
    MainAgent mainAgent;
    Worker worker;
    Submit submitTurn;
    Submit submitJudge;
    MemberProvider memberProvider;
};

#endif // ENGAGEMENT_SERVICE_H

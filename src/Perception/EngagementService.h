#ifndef ENGAGEMENT_SERVICE_H
#define ENGAGEMENT_SERVICE_H

/*
 * 群聊话题会话（意志层 v2）：每群至多一个存活会话，一次话题跟进的完整
 * 生命周期——激活 → 轮次循环 → 结束保留 2h → 清扫/被新会话覆盖。
 *
 * 激活两条路：@ 是硬激活（必须回答走既有回复路径，这里只开会话）；
 * 提名字无 @ 直接开会话并出入场轮——是不是在叫她由她本人（人格主模型）
 * 读现场判断，可 leave_topic 沉默；不再经杂务模型判定器（2026-10-31）。
 * 激活预算（每群 60s 最小间隔 + 每小时上限）只是资源护栏。
 * 会话存活期间，新的点名/提及一律并入本会话，不再重复激活；
 * 其中 @ 只算活动（回答由 Message 的 @ 路径给出），提名字/引用回复她才触发会话轮。
 *
 * 轮次循环：主 Agent（人格 system + 公共上下文材料 + 动作工具表）每轮
 * 三选一——group_say（说话）/ 不调用工具（pass，连续 3 拍收场）/
 * leave_topic（离场，可带最后一句）。发进群里的只有 group_say 的话与离场最后一句：
 * 不调工具、直接写出的文字按沉默处理，不发出（L3 护栏，判定见 Application/EngagementTurn）。
 * 定案前可走至多 3 趟工具轮（recall_context 召回 + setTools 开放的注册表工具，
 * 如 inspect_image / generate_image / klein_web_search），结果回灌后再定案；
 * 工具产出的出站消息（图）当场投递并入群缓冲。
 * 工具轮走 Application/ToolLoop（与一对一对话共用同一实现），开放工具的使用规则
 * 由工具自带、经 Application/ToolGuidance 组装（GroupTurn 场景）。
 * 材料每一行经 Perception/GroupLineFormat（唯一的行格式化点），画出回复的是谁的哪句。
 * 事实卡（经历层 E0，ADR-026）：affect.enabled 为真且接了部署者事实时，契约最前是部署者
 * 事实段（进 system），现场事实（日期、星期、时刻）作为一行放在材料之后（本轮 user 侧）。
 * 先定、再说、后核（经历层 E1，ADR-027）：affect.enforce 为真且 judge 给出计划 v2 时，契约只留
 * 机制（怎么说、怎么沉默、怎么离场），不追加服务契约，提问语换成中性的“以上是群聊现场。”加本轮
 * 任务句；她要说出口的那句经 affect::checkReply 结构核对，不合格重说一次。影子期、回退基线、
 * affect 关着时请求与此前逐字相同。
 * 轮次触发：点名/引用回复她（立即）、追问窗口（她开口后 120s 内群友接话 + 静默，
 * 每次开口至多 2 轮）、lull（攒 5 条 + 静默 + 节流 + 每小时帽）——都只是资源预算，
 * 每轮说不说由她决定。
 *
 * 在场账本（ADR-024，2026-11-16）：每群一份 affect::GroupPresence，是两条发言路径
 * （Message 的 @ 回复、这里的会话轮）共用的唯一事实来源。触发规则管“什么时候看一眼”
 *（模型调用开销），账本管“能不能主动开口”（发言份额）：
 *   - 发言份额只记账、写进情境卡交给判定器（2026-11-21 部署者定规：入场出场由她定，程序只留底层
 *     护栏与资源预算；此前份额用完就不提交没人叫她的轮次）；她自己请求的退场期内不提交；
 *     被叫到的轮次（点名、引用回复、提名字入场）与 @ 回复是应答义务；
 *   - 她说出口的每一句都按“应答 / 主动”记进账本：会话轮在定案处记，@ 回复经
 *     onAtAnswered 记——她自己说话永远不会让份额变多；
 *   - 离场是两条路径共用的同一个动作（会话轮的 leave_topic 动词、@ 回复路径的
 *     leave_topic 工具，都落到 leaveTopic）。带 step_back 即暂时退开：结束跟进，
 *     退场期内不主动开口、点名与冷启动不再开会话；被 @ 仍由 @ 路径照常回答。
 * 硬后盖只有资源预算：会话消息量上限、群内不活跃时长任一触顶强制结束；连续沉默不再收场
 *（2026-11-21）。冷启动的当日帽与冷却同日去掉，只留“同一波热度只看一眼”。
 *
 * 结束不是删除：会话（含话题行）保留 2 小时；保留期内同群新激活 =
 * 清旧建新（旧上下文让位），2h 无续接由 pump 清扫。进程重启即失
 * （短时状态，群内容库才是持久层）。
 *
 * 线程模型：onGroupMessage/onAtActivated/onAtAnswered/leaveTopic 在 worker 线程，
 * pump 在 pollingThread，runTurn 经 submit seam 在群 lane worker
 * 执行——内部 mutex 统一保护，模型调用一律在锁外。
 */
#include "../Affect/AffectOptions.h"
#include "../Affect/GroupPresence.h"
#include "../Application/BotIdentity.h"
#include "../utils/Live.h"
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

namespace affect
{
class DecisionPolicy;
class DecisionRecorder;
class DeployerFacts;
class KnowledgeLedger;
}
class ToolRegistry;

enum class EngagementState
{
    Active, // 存活：接受消息喂食、可触发轮次
    Ended,  // 结束：保留 2h 供续接判断，之后清扫
};

struct EngagementSession
{
    EngagementState state = EngagementState::Active;
    std::uint64_t groupId = 0;
    std::string topicLine;           // 话题一句话（激活时=触发句，可被轮次刷新）
    std::int64_t startTs = 0;
    std::int64_t lastActivityTs = 0; // 群内最近新消息（不活跃收场的基准）
    std::int64_t lastTurnTs = 0;     // 她最近一轮（节流基准）
    std::int64_t lastSpeakTs = 0;    // 她最近一次开口（会话轮或 @ 回复；追问窗口基准）
    int followUpTurnsUsed = 0;       // 本次开口后已用的追问轮数（她再开口归零）
    bool lastMessageAtSelf = false;  // 最新一条群消息 @ 了她：归 @ 路径回答，追问窗口不接（防双回）
    std::int64_t endTs = 0;          // Ended 生效时刻（保留期起算）
    int turnCount = 0;
    int consecutivePasses = 0;
    int newMessagesSinceTurn = 0;    // lull 轮次触发的新消息门槛
    int messagesSeen = 0;            // 会话存活期间经过的群消息量（强制收场上限）
    int recallUsed = 0;              // 上下文召回预算（每会话 1 次）
    std::deque<std::int64_t> recentTurnTs; // lull 轮次时刻（每小时帽；点名/入场/追问轮不计）
    std::string moodLine;                  // 当前情绪基调一句话（每 N 个发言轮经 worker 更新）
    int turnsSinceMood = 0;                // 距上次情绪基调更新的发言轮数
    std::string digest;              // 已压缩部分的滚动摘要
    std::int64_t compressedUpToTs = 0; // 摘要覆盖到的时刻（之后为原文窗口）
    bool addressPending = false;     // 被点名/被回复，待回应（优先触发）
    bool followUpPending = false;    // 追问窗口轮待执行（只为给决策卡标 follow_up_window）
    bool entryPending = false;       // 提名字激活后的入场轮待执行（@ 激活无入场轮）
    bool coldPending = false;        // 冷启动入场轮待执行（热度尖峰自决）
    bool coldInitiated = false;      // 本会话由冷启动开启（结束冷却更久）
    // 当场察觉（经历层 E2，ADR-028）：她在本会话里最近一次开口的会话轮决策 id（记录器开着才有）。
    // 下一轮判定写下 reaction_to_last 时，那条读法记成这次决策的反馈（source=in_the_moment）；
    // 她经 @ 路径开口后清空——之后的反应是对那句 @ 回复的，不归这次决策
    std::string lastSpeechDecisionId;
    std::string endReason;
};

class EngagementService
{
public:
    // 主模型会话轮 seam（组合根适配 ChatService::requestInCharacter——人格
    // system + 调用方 history/tools，单次请求不落库；测试注入 fake）
    using MainAgent = std::function<ChatResponse(const std::string &systemNote,
                                                 const std::vector<ChatMessage> &history,
                                                 const std::vector<std::string> &toolSchemas)>;
    // 杂务模型 seam（组合根适配 ChatService::buildOnceWith——上下文压缩、
    // 情绪基调等后台苦力任务；空/失败返回空串）
    using Worker = std::function<std::string(const std::string &systemPrompt,
                                             const std::string &userPrompt)>;
    // 任务提交 seam（组合根适配 KeyedTaskScheduler::submit，按群占 lane；
    // 同群串行、不阻塞 worker/polling 线程；测试注入同步执行器）
    using Submit = std::function<void(std::uint64_t groupId)>;
    // 群人数查询 seam（组合根适配 GroupListService 镜像；冷启动阈值的人数
    // 地板/先验用；查不到返回 0）。测试注入常量
    using MemberProvider = std::function<long(std::uint64_t groupId)>;
    // 她的出站入库 seam（组合根适配 GroupContextService::recordOutbound）：会话轮里她
    // 发出的一切——group_say 的话、离场最后一句、工具产出的图——都经它记进群缓冲，与
    // @ 路径共用同一条入库实现（占位符、回执关联键、会话活动）。先入库后投递（R0.2）。
    // 可空 = 只投递不入库
    using OutboundRecorder = std::function<void(std::uint64_t groupId,
                                                const OutboundMessage &message,
                                                const std::string &correlationId)>;

    // affectOptions：组合根的活值（ADR-023），每个会话轮开头取一次快照（策略选择与 enforce）
    // bot：组合根的活值（ADR-023），每个会话轮开头取一次（只用名字）
    EngagementService(const Live<BotIdentity> &bot, GroupContextStore *store,
                      const Live<AffectOptions> &affectOptions,
                      MessageSenderPort *sender = nullptr,
                      std::function<std::int64_t()> clock = {});

    void setMainAgent(MainAgent mainAgent) { this->mainAgent = std::move(mainAgent); }
    // 计划 v2 生效时的主模型 seam（经历层 E1，ADR-027）：同 MainAgent，但人格之后不追加服务契约
    //（组合根适配 requestInCharacter(..., serviceContract=false)）。可空 = 用 mainAgent
    void setPlannedAgent(MainAgent plannedAgent) { this->plannedAgent = std::move(plannedAgent); }
    void setWorker(Worker worker) { this->worker = std::move(worker); }
    void setSubmitTurn(Submit submitTurn) { this->submitTurn = std::move(submitTurn); }
    void setMemberProvider(MemberProvider memberProvider)
    {
        this->memberProvider = std::move(memberProvider);
    }
    void setOutboundRecorder(OutboundRecorder outboundRecorder)
    {
        this->outboundRecorder = std::move(outboundRecorder);
    }
    // M1 影子记录：可空；非空且启用时记录每轮决策/执行，不改变动作
    void setDecisionRecorder(affect::DecisionRecorder *recorder)
    {
        this->decisionRecorder = recorder;
    }
    // R1 决策层：可空（= 基线）。非基线策略且记录器启用时，每轮在请求主模型前装配
    // 情境卡并 decide，记录换成按策略构造的决策；enforce=false（影子）时之后的流程
    // 与基线逐字相同（enforce 来自构造时给的活值）。policy 由组合根持有、须比本服务活得久；
    // 组合根给的是策略外壳，每个会话轮按 affect 快照 resolve 一次
    void setPolicy(affect::DecisionPolicy *policy) { this->policy = policy; }
    // 事实卡（经历层 E0，ADR-026）：可空 = 不加事实卡。非空且当轮 affect.enabled 为真时，部署者
    // 事实段排在契约最前（进 system），现场事实作为一行放在材料之后（本轮 user 侧）。
    // facts 由组合根持有、须比本服务活得久；每轮现读一次文件
    void setDeployerFacts(const affect::DeployerFacts *facts) { this->deployerFacts = facts; }
    // 取用（经历层，ADR-029）：可空 = 不取用。非空且当轮 affect.enabled 为真时，从账本取全局的、本群的、
    // 现场的人（材料窗口里的发言者 + 回复候选）的认识：judge 的 prompt 尾部一份，表达请求在现场事实
    // 那一行之后、提问语之前一份；编号写进决策记录的 state_json。ledger 由组合根持有、须比本服务活得久
    void setKnowledgeLedger(affect::KnowledgeLedger *ledger) { this->knowledgeLedger = ledger; }
    // 会话轮可用的注册表工具（如 inspect_image / generate_image / send_image /
    // klein_web_search / klein_web_fetch；契约追加开放工具自带的 GroupTurn 使用规则）：
    // tools 由组合根持有、须比本服务活得久；allowedNames 当场过滤——未注册或
    // requiresAdmin() 的名字丢弃并告警（L3 护栏：管理员工具永不开放给会话轮）。
    // 名单里当前不可用的工具（Tool::available）留在名单里，每轮开头按可用性再筛一次。
    // 工具的出站消息直接投递到群并经 OutboundRecorder 入库。不调用 = 无注册表
    // 工具，会话轮 prompt 与工具表与此前一致
    void setTools(const ToolRegistry *tools, std::vector<std::string> allowedNames);

    // worker 线程：监控群新消息（record 已入库）。会话存活期喂活动/点名；
    // 无存活会话时提名字无 @ 直接开会话并提交入场轮（激活预算内）
    void onGroupMessage(const GroupMessageRecord &record, bool atMentioned);

    // @ 硬激活（Message 群 @ 路径调用）：清旧建新开会话；回答本身走既有
    // 回复路径，不入场轮。会话已存活时仅记活动
    void onAtActivated(std::uint64_t groupId, const std::string &triggerText);

    // 她经 @ 路径回答了（Message 在确有出站时调用一次，分条发送也只调一次）：
    // 在场账本记一句应答（不占份额）；会话存活时算会话活动并打开追问窗口
    void onAtAnswered(std::uint64_t groupId);
    // 离场（ADR-024，两条路径共用）：结束该群存活的会话；stepBack=true 时同时进入退场期。
    // @ 回复路径的 leave_topic 工具经组合根接到这里；会话轮的 leave_topic 动词在 runTurn 里走同一逻辑
    void leaveTopic(std::uint64_t groupId, bool stepBack, const std::string &reason);

    // pollingThread 每 3s：硬后盖（消息量/不活跃）+ 保留期清扫 + 追问窗口/lull 轮次触发
    void pump(std::int64_t now);

    // 群 lane worker 内执行：一轮跟进（装配材料 → 主 Agent → 定案（Application/EngagementTurn）
    // → 动作分发）
    void runTurn(std::uint64_t groupId);

    // token 启发式估算（预算门控用，非精确计价）：CJK 码点 ≈1 token/字，
    // ASCII ≈1 token/4 字符
    static std::size_t estimateTokens(const std::string &text);

    // 调试/测试观测
    std::optional<EngagementSession> sessionOf(std::uint64_t groupId) const;
    affect::PresenceView presenceOf(std::uint64_t groupId) const;

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
    // 离场的唯一实现（锁内）：记离场、按 stepBack 进入退场期、结束存活的会话
    void leaveLocked(std::uint64_t groupId, std::int64_t now, bool stepBack,
                     const std::string &reason);
    affect::GroupPresence &presenceLocked(std::uint64_t groupId) { return presences[groupId]; }
    // correlationId：回执关联键（R0.2）；投递与自消息入库共用同一个，回执到达后
    // 执行记录推进 sent/failed/unknown、自消息补上真实 message_id
    void deliverMessage(std::uint64_t groupId, const OutboundMessage &message,
                        const std::string &correlationId = {});
    // 她的一条出站：先经 OutboundRecorder 入群缓冲，再投递（会话轮唯一出站路径）
    void emitOutbound(std::uint64_t groupId, const OutboundMessage &message,
                      const std::string &correlationId = {});
    bool replyToSelf(const GroupMessageRecord &record) const;
    // 提名字激活预算（资源护栏）：每群 60s 最小间隔 + 每小时上限；放行即记账
    bool allowNameActivationLocked(std::uint64_t groupId, std::int64_t now);
    // 材料装配：原文窗口（超长保头截断）+ 预算内直灌；超预算把最旧一段经
    // worker 合并进滚动摘要，只留原文尾巴。worker 调用在锁外由调用方保证
    PreparedContext prepareContext(const BotIdentity &bot, const std::vector<GroupMessageRecord> &records,
                                   const std::string &digest,
                                   std::int64_t compressedUpToTs,
                                   const std::string &moodLine) const;
    std::string runContextRecall(const BotIdentity &bot, const nlohmann::json &arguments,
                                 std::uint64_t groupId,
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
        std::int64_t dayAnchor = 0;      // 日界锚点：当天本地零点的时间戳
        int coldToday = 0;               // 今日（本地日历日）冷启动评估次数：只计数进日志，不再限制
        std::int64_t lastEvaluation = 0; // 上次冷启动评估时刻：同一波热度只看一眼（纯内存）
    };

    // 冷启动：基线学习 + 热度尖峰检测 + 频控（锁内调用，pump 每 3s 触发、
    // 内部 60s 节流）；返回 true 表示命中尖峰、由调用方开冷启动会话
    bool coldCheckLocked(std::uint64_t groupId, ColdState &cold, std::int64_t now);
    // 学习/频控状态落库（UPSERT engagement_cold；锁内调用，低频）
    void persistColdLocked(std::uint64_t groupId);

    const Live<BotIdentity> &botIdentity;
    GroupContextStore *const store;
    const Live<AffectOptions> &affectOptions;
    MessageSenderPort *const sender;
    const std::function<std::int64_t()> clock;
    mutable std::mutex mutex;
    std::map<std::uint64_t, EngagementSession> sessions;
    std::map<std::uint64_t, ColdState> colds;
    std::map<std::uint64_t, std::deque<std::int64_t>> nameActivationHistory; // 激活预算记账
    std::map<std::uint64_t, affect::GroupPresence> presences; // 在场账本（ADR-024），会话锁保护
    MainAgent mainAgent;
    MainAgent plannedAgent;
    Worker worker;
    Submit submitTurn;
    MemberProvider memberProvider;
    OutboundRecorder outboundRecorder;
    affect::DecisionRecorder *decisionRecorder = nullptr;
    affect::DecisionPolicy *policy = nullptr;
    const affect::DeployerFacts *deployerFacts = nullptr;
    affect::KnowledgeLedger *knowledgeLedger = nullptr;
    const ToolRegistry *tools = nullptr;
    std::vector<std::string> turnToolNames; // setTools 过滤后的会话轮注册表工具名
};

#endif // ENGAGEMENT_SERVICE_H

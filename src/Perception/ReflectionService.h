#ifndef REFLECTION_SERVICE_H
#define REFLECTION_SERVICE_H

/*
 * 回想服务（经历层 E3，ADR-030；设计 §6.0 学习观、§6.2）：定时把她最近的经历交给她自己想一遍，
 * 读法记成反馈、认识的增改记进账本。prompt 与解析在 Affect/Reflection（纯函数），这里管材料装配、
 * 调度与线程。
 *
 * 一次回想看什么（全部在内存里装配，原文不落库）：该群游标之后、后续窗口（20 分钟）已关闭的决策，
 * 按（时刻，决策号）排序，至多 10 次——开口、沉默、离场都是经历（2026-11-21 部署者定规：她的每个动作都该回想）；每次带计划里的
 * 场合、动作与做法（影子期标出“没有生效”）、她说的话或“没开口”、之后至多 30 条现场（群缓冲里还在的）、
 * 已有的读法（当场察觉等）；再带一段她没参与的群聊（最后一次经历的现场之后、不是她说的，至多 60 条）、
 * 她现在的认识（本群范围内有效的，标出教导）、在场的人（材料里出现过的群友）、她的人格与关于她的事实、此刻。
 *
 * 游标不早于 24 小时前（群缓冲的保留期；更早的经历没有现场可看，第一次回想不从头翻旧账）；模型没返回
 * 时退避 30 分钟再自动试（2026-11-22 真机：第一轮从 10 月底的决策翻起，另一群超时后每分钟重试）。
 * 调度（资源预算，代码常量 ReflectionRules）：pollingThread 每 3 秒调 pump，内部 60 秒节流；每群
 * 自动回想的条件是“有待回想的经历”且“距上次 ≥ 6 小时，或最早一次未回想经历之后群缓冲已积 200 条
 *（原文快过期了）”，每群每天（本地日历日）至多 4 次。部署者在私聊里经 reflect_now 工具手动触发：
 * 不受间隔、当日帽与退避限制，仍要有待回想的经历——与自动调度同一判据（同样套 24 小时地板）；
 * 已排队或正在回想的群不重复入队，如实报给工具。进度游标与计数存 perception_meta（键
 * affect.reflection.<群号>，JSON），不新增表。游标是（时刻，决策号）：决策时刻只到秒，同一秒的决策
 * 可能多于一次回想的上限，所以记下最后看过的决策号，下次从同一秒里它之后的那条接着看；旧进度没有
 * 决策号时按“这一秒都看过了”处理。
 *
 * 线程（技术设计 §22.7）：本类拥有一条工作线程（start 起、shutdown 收），输入是一个按群去重的
 * 有界队列（每群至多排一次），pump / requestNow 入队后 notify 唤醒；runNow 在工作线程上执行，模型
 * 调用在所有锁外。shutdown 丢弃队列、置停止标志、join；进行中的模型调用随 running=false 由适配器
 * 取消，返回空后这次回想按“模型没返回”结束：不记读法、不写账本、游标不动，只在进度里写上退避截止
 *（retry_after）。同群不会并行（队列去重 + 线程只有一条），游标在读法与账本写完之后才前移，所以一次
 * 经历至多回想一次；进程中途退出最多丢这一次回想，下次重来。
 * 回想期间部署者按群清除认识：apply 在自己的事务里写，清除也是事务，先后都成立，不做协调。
 * 失败只记日志（条数与 id，没有正文）。析构顺序：组合根在决策库、记录器、账本、群内容库之后构造本类，
 * 先于它们析构；pollingThread 在 shutdown 之后才 join，shutdown 后的 pump 是空操作。
 *
 * 开关：affect.enabled 当前为假时 pump 与 runNow 什么都不做、不打开账本；为真时按与账本工具同一判据打开。
 */
#include "../Affect/AffectOptions.h"
#include "../Affect/Reflection.h"
#include "../Application/BotIdentity.h"
#include "../utils/Live.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

class GroupContextStore;

namespace affect
{
class DecisionRecorder;
class DecisionStore;
class DeployerFacts;
class KnowledgeLedger;

// 每群的进度（perception_meta 里的 JSON）
struct ReflectionProgress
{
    std::int64_t cursor = 0;    // 已回想到的决策 created_at（之前的不再看）
    // cursor 那一秒里最后看过的决策号（JSON 字段 cursor_decision）：同一秒里号更大的还没看。
    // 空 = cursor 那一秒的都看过了（旧进度没有这个字段，也按这样读）
    std::string cursorDecision;
    std::int64_t lastRun = 0;   // 上次回想（自动或手动，调过模型且有结果）的时刻；自动调度的间隔从它算
    std::int64_t day = 0;       // runsToday 对应的本地日零点
    int runsToday = 0;          // 今日自动回想次数
    std::int64_t retryAfter = 0; // 模型没返回后的退避截止；自动回想在此之前不再试（手动不受限）
};

// 一次手动请求的结果（reflect_now 的回复用）
struct ReflectionRequest
{
    std::size_t scheduled = 0; // 这次新排上的群
    std::size_t running = 0;   // 正在回想的群（这次没再排）
    std::size_t waiting = 0;   // 已在队列里等着的群（这次没再排；那一次升级成手动）
    bool unavailable = false;  // affect 关着、账本打不开或服务已停：什么都没看
};

// 一次回想的结果（日志与工具回复用；没有正文）
struct ReflectionReport
{
    bool ran = false;           // 调过模型
    std::string skipped;        // 没跑的原因（空 = 跑了）
    std::string runId;
    std::string modelLabel;
    std::size_t experiences = 0;
    std::size_t readings = 0;   // 记下的读法
    std::size_t written = 0;    // 账本写入的改动
    std::size_t rejected = 0;   // 读法 + 改动里被挡掉的
    std::size_t decayed = 0;    // 这次顺带遗忘的条数
    long latencyMs = 0;
};

class ReflectionService
{
public:
    using Worker = std::function<std::string(const std::string &systemPrompt,
                                             const std::string &userPrompt)>;
    struct Target
    {
        Worker worker;
        std::string modelLabel; // 模型名，不含端点/密钥
    };
    // 每次回想开头调用一次：按当前配置（memory.model、models.worker、注册表）解析调用目标
    using TargetResolver = std::function<Target()>;
    using TextSource = std::function<std::string()>;

    // store / recorder / ledger 由组合根持有、须比本对象活得久；groups 可空（观察通道关着：没有现场，
    // 回想只看她的话与已有读法）。clock 空 = 系统时钟
    ReflectionService(DecisionStore &store, DecisionRecorder &recorder, KnowledgeLedger &ledger,
                      const GroupContextStore *groups, const Live<AffectOptions> &affectOptions,
                      const Live<BotIdentity> &bot, TargetResolver resolver,
                      std::function<std::int64_t()> clock = {});
    ~ReflectionService();

    ReflectionService(const ReflectionService &) = delete;
    ReflectionService &operator=(const ReflectionService &) = delete;

    // 她的人格 / 关于她的事实（与判定器同一来源，每次回想现读）；空 = 不加
    void setSoulSource(TextSource source) { soulSource = std::move(source); }
    void setFactsSource(TextSource source) { factsSource = std::move(source); }

    // 起工作线程（组合根接线完成后调用一次）；shutdown 丢弃队列并 join，之后 pump / requestNow 空操作
    void start();
    void shutdown();

    // pollingThread 每 3 秒：60 秒节流，把到期的群入队
    void pump(std::int64_t now);
    // 手动触发（reflect_now 工具）：groupId=0 表示所有有过决策的群。已排队或正在回想的群不重复入队、
    // 分别计数；其余的群有待回想经历（与自动调度同一判据，套 24 小时地板）才入队
    ReflectionRequest requestNow(std::uint64_t groupId);

    // 同步执行一次回想（工作线程调用；测试直接调）。manual=true 不计入当日帽、不看间隔
    ReflectionReport runNow(std::uint64_t groupId, bool manual);

    // 此刻该自动回想的群（测试观测；pump 用它）
    std::vector<std::uint64_t> dueGroups(std::int64_t now);
    ReflectionProgress progressOf(std::uint64_t groupId) const;

private:
    struct Job
    {
        std::uint64_t groupId = 0;
        bool manual = false;
    };
    enum class QueueState
    {
        Added,   // 这次入队
        Waiting, // 已在队列里（manual 时把那一次升级成手动）
        Running, // 工作线程正在回想它
        Stopped  // 服务已停
    };

    bool enabled() const;
    QueueState enqueue(std::uint64_t groupId, bool manual);
    bool isQueued(std::uint64_t groupId) const; // 已排队或正在回想
    // 游标地板：不早于 24 小时前；地板高于游标时清掉 cursorDecision
    static void floorCursor(ReflectionProgress &progress, std::int64_t now);
    void workerLoop();
    ReflectionProgress loadProgress(std::uint64_t groupId) const;
    bool saveProgress(std::uint64_t groupId, const ReflectionProgress &progress);
    // 游标（progress 的 cursor 与 cursorDecision）之后、后续窗口已关闭的经历（至多 limit 次）；
    // scanned 带出扫过的最后一条决策的位置（cursor = 它的时刻，cursorDecision = 它的决策号），
    // 回想完把游标推到这里；没扫到决策时等于传入的游标
    std::vector<ReflectionExperience> pendingExperiences(std::uint64_t groupId, const ReflectionProgress &from,
                                                         std::int64_t now, std::size_t limit,
                                                         ReflectionProgress &scanned) const;

    DecisionStore &store;
    DecisionRecorder &recorder;
    KnowledgeLedger &ledger;
    const GroupContextStore *const groups;
    const Live<AffectOptions> &affectOptions;
    const Live<BotIdentity> &botIdentity;
    const TargetResolver resolver;
    const std::function<std::int64_t()> clock;
    TextSource soulSource;
    TextSource factsSource;

    std::int64_t lastPump = 0;
    mutable std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::deque<Job> queue;
    std::set<std::uint64_t> queued;
    std::thread worker;
    bool started = false;
    bool stopping = false;
};
} // namespace affect

#endif // REFLECTION_SERVICE_H

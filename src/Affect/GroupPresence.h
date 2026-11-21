#ifndef AFFECT_GROUP_PRESENCE_H
#define AFFECT_GROUP_PRESENCE_H

/*
 * 她在一个群里的在场账本（2026-11-16，ADR-024）。两条发言路径（被 @ 的回复、话题会话轮）
 * 共用的唯一事实来源：群里别人说了什么、她说了什么、她是不是暂时退开了。
 *
 * 2026-11-21 起这是一本账，不是门（部署者定规：入场出场由她定，程序只留底层护栏与资源预算）：
 * 窗口内的计数写进情境卡交给判定器，mayVolunteer 只剩观测与测试用；退场仍生效，因为那是她自己的决定。
 *
 * 发言份额：她没被叫到时主动说的话（Unprompted），在最近 shareWindowSeconds 里不超过
 * 其他人平均每人说的条数——即 (她的主动条数 + 1) × 其他发言人数 ≤ 其他人的消息总数。
 * 多人热聊时她不比一般群友话多；一对一来回时，对方说一句她能接一句。
 *   - 只有她主动开口消耗份额；份额只随别人说话、随时间滑出窗口而恢复。她自己说话永远
 *     不会让份额变多（2026-11-16 真机：此前追问预算“她一开口就归零”，模型越爱说越说不完）。
 *   - 被叫到时的应答（Answer：被 @、被点名、被引用回复）是义务，不占份额，只记账。
 *   - 窗口里没有任何别人的消息时不主动开口（没有可接的话）。
 *
 * 退场：被请走时（离场动作带 step_back）进入，持续 withdrawSeconds。期间不主动开口，
 * 也不因点名或冷启动重新开始跟进；被 @ 仍由 @ 路径照常回答。
 *
 * 纯逻辑：不加锁、不记日志、不读时钟（时间由调用方传入）。EngagementService 持有每群一份，
 * 在它的会话锁内调用。进程内状态，重启即空（与话题会话同一口径）。
 */
#include <cstdint>
#include <deque>

namespace affect
{
struct PresenceRules
{
    std::int64_t shareWindowSeconds = 10 * 60; // 发言份额的观察窗口
    std::int64_t withdrawSeconds = 30 * 60;    // 被请走后的退场时长
};

enum class SpeechKind
{
    Answer,     // 被叫到后的应答：不占份额
    Unprompted, // 没人叫她时主动说的话：占份额
};

// 只读观测（日志、测试、决策卡扩展用）；计数都只看窗口内
struct PresenceView
{
    int othersMessages = 0;
    int otherSpeakers = 0;
    int answers = 0;
    int unprompted = 0;
    bool withdrawn = false;
    std::int64_t withdrawnUntil = 0; // 0 = 从未退场
    bool mayVolunteer = false;
};

class GroupPresence
{
public:
    explicit GroupPresence(PresenceRules rules = {}) : rules(rules) {}

    // 群里别人的一条消息（她自己的不经这里）
    void noteOthersMessage(std::int64_t at, std::uint64_t speakerId);
    // 她说出口的一句（一次发言记一条，分条发送也只记一条）
    void noteSpeech(std::int64_t at, SpeechKind kind);
    // 被请走：退场到 at + withdrawSeconds（已在退场中则取更晚者）
    void withdraw(std::int64_t at);

    bool withdrawn(std::int64_t now) const;
    // 现在能否主动开口：未退场，且发言份额还有
    bool mayVolunteer(std::int64_t now) const;
    PresenceView view(std::int64_t now) const;

private:
    enum class Who
    {
        Other,
        Answer,
        Unprompted,
    };
    struct Entry
    {
        std::int64_t at = 0;
        Who who = Who::Other;
        std::uint64_t speakerId = 0;
    };

    void append(Entry entry);

    PresenceRules rules;
    std::deque<Entry> recent; // 大致按时间排序；查询按窗口过滤，写入时修剪
    std::int64_t withdrawnUntil = 0;
};
} // namespace affect

#endif // AFFECT_GROUP_PRESENCE_H

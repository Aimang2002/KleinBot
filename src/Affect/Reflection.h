#ifndef AFFECT_REFLECTION_H
#define AFFECT_REFLECTION_H

/*
 * 回想（经历层 E3，ADR-030；设计 §6.0 学习观、§6.2）的纯函数部分：输入怎么拼成 prompt、模型输出
 * 怎么解析与校验。不碰数据库、不调模型、不看时钟——材料的装配与调度在 Perception/ReflectionService。
 *
 * 她事后把最近几次经历（场合、她的打算、她说的话、之后的现场、已有的读法）连同一段她没参与的群聊、
 * 她现在的认识、她的人格与事实一起看一遍，用第一人称写两样东西：
 *   - 每次经历的读法（polarity 好 / 尴尬 / 坏 / 看不出），记成那次决策的反馈（source=self_reflection）；
 *     沉默、离场也是经历（2026-11-21 部署者定规：她的每个动作都该回想），一样写读法；
 *   - 认识的增改（add / modify / strengthen / weaken / withdraw），交给账本 apply。
 * 什么算好、什么算坏、哪次经历印象深到值得写成认识，由她判断；这里的提示只讲机制与学习观的规矩
 *（设计 §6.0：强信号才写、看别人的先低强度再靠后果验证、部署者的话分量重但不是命令、与人格事实冲突不写），
 * 不写她该怎么做人。隐私：读法与认识都是派生文本，照抄现场原文的由 KnowledgeGuard 挡掉。
 */
#include "KnowledgeLedger.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace affect
{
// 规则常量（资源预算与护栏，不是配置）
struct ReflectionRules
{
    static constexpr std::int64_t kIntervalSeconds = 6 * 3600; // 每群自动回想的最小间隔
    static constexpr int kDailyCap = 4;                        // 每群每天（本地日历日）自动回想次数
    static constexpr std::int64_t kAftermathSeconds = 20 * 60; // 后续窗口：她那句之后至少这么久才算“后来怎样”已定
    static constexpr std::size_t kAftermathMessages = 30;      // 每次经历之后至多带这么多条现场
    static constexpr std::size_t kBystanderMessages = 60;      // 她没参与的群聊至多这么多条
    static constexpr std::size_t kExperiencesPerRun = 10;      // 一次回想至多看几次经历（沉默也算经历）
    static constexpr std::size_t kEarlyTriggerMessages = 200;  // 最早一次未回想经历之后群缓冲已积这么多条就提前回想（300 的三分之二）
    static constexpr std::size_t kMaxReadings = 16;            // 模型一次最多给的读法条数（多的丢弃）
    static constexpr std::size_t kMaxReadingCodepoints = 120;  // 读法一句的上限（与计划 v2 的派生字段一致）
    static constexpr long kTimeoutMs = 60000;                  // 模型调用超时：后台任务，可以等久一点
};

// 一条现场（已经格式化成一行的群聊材料，和它的原文——原文只用来挡照抄，不进 prompt 以外的地方）
struct ReflectionLine
{
    std::string line; // 经 GroupLineFormat 的一行
    std::string raw;  // 这条消息的原文
};

// 一次经历：一次决策加上她的话、之后的现场、已有的读法
struct ReflectionExperience
{
    std::string decisionId;
    std::int64_t at = 0;            // 她说那句的时刻（执行记录的时间）
    std::string timeText;           // 本地时间 HH:MM（由装配方给）
    std::string source;             // engagement_turn / at_reply / …
    std::string occasion;           // 计划 v2 的场合（可空）
    std::string action;             // 计划的动作：group_say / reply / no_reply / leave_topic
    std::string move;               // 计划 v2 的做法（可空）
    bool shadow = false;            // 影子期：判定器的计划没有生效，她做的是基线路径的事
    bool spoke = false;             // 她开口了（执行记录有她的话）
    std::string spoken;             // 她实际说出口的话（执行记录）；没开口为空
    std::vector<ReflectionLine> aftermath; // 她那句之后的现场（可空：原文已过期）
    // 已有的读法：feedback_id + 读法文字（当场察觉等）
    std::vector<std::pair<std::string, std::string>> readings;
};

// 她现在的认识（本群范围内有效的）
struct ReflectionKnowledge
{
    std::int64_t id = 0;
    std::string kind;
    std::uint64_t scopeGroup = 0;
    std::uint64_t scopeUser = 0;
    std::string text;
    bool teaching = false; // 教导过的：回想改不了，prompt 里标出来
};

// 在场的人：QQ 号与昵称快照（person 认识只能写给他们）
struct ReflectionPerson
{
    std::uint64_t userId = 0;
    std::string nickname;
};

// 一次回想的全部输入
struct ReflectionInput
{
    std::uint64_t groupId = 0;
    std::string botName;
    std::string soul;              // 她的人格原文（可空）
    std::string deployerFacts;     // 关于她的事实原文（可空）
    std::string sceneNote;         // 此刻（sceneFactsNote）
    std::string managerNickname;   // 部署者在这个群的昵称（可空 = 他没在材料里出现）
    std::vector<ReflectionExperience> experiences;
    std::vector<ReflectionLine> bystander; // 她没参与的一段群聊
    std::vector<ReflectionKnowledge> knowledge;
    std::vector<ReflectionPerson> people;
};

// 模型输出：读法
struct ReflectionReading
{
    std::string decisionId;
    std::string reading;
    std::string polarity; // good / awkward / bad / unclear
    double confidence = 0.0;
};

struct ReflectionOutput
{
    std::vector<ReflectionReading> readings;
    std::vector<KnowledgeChange> changes; // 已翻成账本的改动请求（key 为空时由装配方补）
};

// 固定的回想提示（机制与学习观的规矩，不写她该怎么做人）
std::string reflectionSystemPrompt();
// 用户侧：她是谁、关于她的事实、此刻、她现在的认识、在场的人、待回想的经历、她没参与的群聊
std::string reflectionUserPrompt(const ReflectionInput &input);

// 解析模型原文：只按 JSON 解析（去掉围栏与前后杂文），结构不对返回空、error 写原因。
// 读法超过 kMaxReadings 条、改动超过 KnowledgeRules::kMaxChangesPerRun 条的多余部分丢弃；
// 改动里的 id 翻成 targetId，scope_group / scope_user 原样带出，由账本按规矩核对
std::optional<ReflectionOutput> parseReflection(const std::string &raw, std::string &error);

// 读法的代码校验（护栏，不判断好坏）：decision_id 在本次经历里、polarity 在封闭集合里、
// 一行且不超过上限、不照抄材料（material = 本次全部现场原文）；不合格返回 false 并写原因
bool checkReading(const ReflectionReading &reading, const std::vector<std::string> &decisionIds,
                  const std::vector<std::string> &material, std::string &reason);

// 回想写的认识没有 key 时由此生成：正文的短哈希（同一句再写落在同一条上）
std::string reflectionKey(const std::string &text);
} // namespace affect

#endif // AFFECT_REFLECTION_H

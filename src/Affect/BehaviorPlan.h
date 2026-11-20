#ifndef AFFECT_BEHAVIOR_PLAN_H
#define AFFECT_BEHAVIOR_PLAN_H

#include "../../Library/nlohmann/json.hpp"

#include <optional>
#include <string>
#include <vector>

namespace affect
{
// 行为计划，写进 affect_decisions.plan_json（不改表结构），按 schema_version 分两版：
//
// v1（R1，基线与回退一直用它）：action 是行为字典的稳定标识；speech_act/tone 由策略填充，
// 基线记 "unspecified"；max_codepoints=0 表示计划层不额外限制。
//
// v2（经历层 E1“先定”，ADR-027，judge 产出）：
//   action            同 v1
//   reply_to          她回应的那条消息的 message_id，可空（null）
//   move              做法，封闭集合 answer/follow/agree/comfort/tease/ask/ack；说话类动作必填
//   length            长度档 short/one/few；说话类动作必填
//   occasion          场合：一句派生描述（转述，不照抄群友原文）。judge 必须给（JudgePolicy::parse
//                     不收空的）；照抄原文时被隐私护栏清空，所以存储层允许为空
//   reaction_to_last  可选：她上一句之后大家的反应与她的读法（派生文本）
//   why               一句理由，只进日志与记录
//   tool_policy / allowed_tools  同 v1
// v2 不用 speech_act / tone / max_codepoints（toJson 不写，fromJson 不读）。
// 读到 v1 记录时按 v1 解析，不迁移旧数据。
//
// tool_policy: inherit=继承入口既有工具集；allow_list=只允许 allowed_tools；none=不提供工具。
struct BehaviorPlan
{
    static constexpr int kSchemaVersion = 1;   // v1（基线、回退）
    static constexpr int kSchemaVersionV2 = 2; // v2（judge，经历层 E1）
    // v2 派生文字字段（occasion / reaction_to_last / why）的码点上限（存储护栏）
    static constexpr std::size_t kMaxNoteCodepoints = 120;
    // reply_to 的字节上限（message_id 的数字或字符串形态）
    static constexpr std::size_t kMaxReplyToBytes = 64;

    int schemaVersion = kSchemaVersion;
    std::string action;
    // v1
    std::string speechAct = "unspecified";
    std::string tone = "unspecified";
    int maxCodepoints = 0;
    // v2
    std::optional<std::string> replyTo;
    std::string move;
    std::string length;
    std::string occasion;
    std::string reactionToLast;
    std::string why;
    // 两版共用
    std::string toolPolicy = "inherit";
    std::vector<std::string> allowedTools;

    bool isV2() const { return schemaVersion == kSchemaVersionV2; }

    bool validate(std::string &error) const;
    nlohmann::json toJson() const;
    static std::optional<BehaviorPlan> fromJson(const nlohmann::json &value,
                                                std::string &error);
};

bool isKnownBehaviorAction(const std::string &action);
bool isKnownToolPolicy(const std::string &toolPolicy);
bool isKnownMove(const std::string &move);     // answer / follow / agree / comfort / tease / ask / ack
bool isKnownLength(const std::string &length); // short / one / few
// 说话类动作（reply / group_say）：v2 的 move 与 length 必填
bool isSpeakingAction(const std::string &action);

} // namespace affect

#endif // AFFECT_BEHAVIOR_PLAN_H

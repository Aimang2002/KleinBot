#ifndef LEAVE_TOPIC_TOOL_H
#define LEAVE_TOPIC_TOOL_H

#include "Tool.h"
#include "ToolArgumentParser.h"
#include "../Port/ToolNames.h"

#include <cstdint>
#include <functional>
#include <string>

/*
 * 离场（ADR-024）的 @ 回复路径一侧：被 @ 时她也能结束自己在这个群的话题跟进，与会话轮的
 * leave_topic 动词同名同义，最终都落到同一处（组合根把 bind 接到 EngagementService::leaveTopic）。
 * 2026-11-16 真机：她在 @ 回复里答应“好，我退开了”，跟进那边却不知道，10 秒后照常接话。
 *
 * 只在群聊开放（offeredIn）；未接线时不可用（available），工具表与使用规则都不提它。
 * 非终止：结果回灌后她照常写回复（或按群聊收敛契约不回应）。
 */
class LeaveTopicTool : public Tool
{
public:
    // groupId、是否暂时退开、理由（只进日志）
    using Leave = std::function<void(std::uint64_t groupId, bool stepBack, const std::string &reason)>;

    // 组合根在跟进服务构造后接线（启动期、线程启动前调用一次）
    void bind(Leave leave) { this->leave = std::move(leave); }

    std::string name() const override { return KleinLeaveTopicToolName; }

    std::string description() const override
    {
        return "结束你在这个群的话题跟进。有人请你别说了、先退下，或你觉得这个话题该停了时调用；"
               "要说的话照常写在回复里。";
    }

    std::string parametersSchema() const override
    {
        return R"({"type":"object","properties":{"reason":{"type":"string","description":"一句话理由（只进日志）"},"step_back":{"type":"boolean","description":"有人请你别说了、先退下时为 true：之后一段时间你不会主动接话，被 @ 仍会回答；只是话题该停了就不填"}},"required":["reason"]})";
    }

    bool available() const override { return static_cast<bool>(leave); }

    bool offeredIn(ToolVenue venue) const override { return venue == ToolVenue::Group; }

    std::string usageGuidance(GuidanceScene scene, const GuidanceContext &) const override
    {
        return guidanceText(scene);
    }

    // 规则文本（静态，金样测试直接取用）。只在一对一对话场景（群里被 @ 时）给出；
    // 会话轮的离场写在会话契约里
    static std::string guidanceText(GuidanceScene scene)
    {
        if (scene != GuidanceScene::Conversation)
            return {};
        return "群里有人请你别说了或先退下时，调用 leave_topic 并把 step_back 设为 true："
               "之后一段时间你不会主动接话，被 @ 仍会回答。";
    }

    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        if (ctx.shared_scope_id == 0)
            return {"这里是私聊，没有群聊话题跟进可以结束。", {}, {}};
        if (!leave)
            return {"错误：离场暂不可用。", {}, {}};
        std::string reason;
        bool stepBack = false;
        try
        {
            const nlohmann::json arguments = parseToolArguments(args);
            if (arguments.is_object())
            {
                if (arguments.contains("reason") && arguments["reason"].is_string())
                    reason = arguments["reason"].get<std::string>();
                if (arguments.contains(KleinLeaveTopicStepBackParam) &&
                    arguments[KleinLeaveTopicStepBackParam].is_boolean())
                    stepBack = arguments[KleinLeaveTopicStepBackParam].get<bool>();
            }
        }
        catch (const std::exception &)
        {
            // 参数坏了也照常离场（理由缺省、不退开）：离场本身没有可出错的输入
        }
        leave(ctx.shared_scope_id, stepBack, reason);
        return {stepBack ? "已退出这个群的话题跟进：一段时间内你不会主动接话，被 @ 仍会回答。"
                         : "已结束这个群的话题跟进。",
                {}, {}};
    }

private:
    Leave leave;
};

#endif // LEAVE_TOPIC_TOOL_H

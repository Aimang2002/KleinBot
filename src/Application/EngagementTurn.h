#ifndef ENGAGEMENT_TURN_H
#define ENGAGEMENT_TURN_H

/*
 * 会话轮定案：模型这一轮的输出里，哪些算她说出口的话。唯一判定点，
 * EngagementService::runTurn 在工具轮结束后调用。
 *
 * 这是 ADR-017 的 L3 程序护栏：硬约束由代码执行，不依赖模型自觉。会话契约告诉她
 * "文字内容只有经 group_say 才会被发出"，这里让代码照这句话办：
 *   - 工具调用里有 group_say：发言，内容是参数 text（text 只有空白时由调用方按沉默处理）；
 *   - 工具调用里有 leave_topic：离场，reason 只进日志，final_text 是离场前留下的那句（可空），
 *     step_back=true 表示被请走、暂时退开（ADR-024，与 @ 回复路径的 leave_topic 工具同义）；
 *   - 工具调用里有 send_voice：发言，内容是参数 text，用语音说出来（voice=true，2026-11-16；
 *     与对话路径的 send_voice 工具同名同义，只在语音可用时开放）；
 *     几个动词出现在同一个回复里时，按调用顺序取第一个；
 *   - 有工具调用，但都不是这两个（只剩未知/未开放工具，或工具轮到顶仍在要工具）：这一拍沉默；
 *   - 没有任何工具调用：一律沉默，不论 content 是否为空。
 * content 不是她说出口的话。她常把"为什么不说话"写成一句旁白，比如"（没有说话，继续留意后续）"；
 * 截至 2026-11-14，主动轮发进群的 459 条消息里有 5 条整句就是这种旁白。没有工具调用时，判定只带出
 * content 去首尾空白后的码点数（unspokenChars），供调用方记日志；原文不带出——它可能引用了群友的话
 * 或昵称，不进日志、不进记录。
 *
 * 只用于会话轮（主动轮）。被 @ 的回复与私聊走 ChatService::reply，那里 content 本身就是回复，不经这里。
 * 纯函数：不持锁、不记日志、不碰会话状态。
 */
#include "../Port/ChatResponse.h"

#include <cstddef>
#include <string>

// 会话轮一拍的动作（say / pass / leave 三选一）
struct EngagementTurn
{
    enum class Kind
    {
        Speak,
        Pass,
        Leave,
    };
    Kind kind = Kind::Pass;
    std::string text;              // Speak：group_say 的 text；Leave：final_text（可空）
    std::string reason;            // Leave：离场理由（仅日志）
    bool stepBack = false;         // Leave：被请走、暂时退开（之后一段时间不主动接话）
    bool voice = false;            // Speak：经 send_voice 说的，用语音送达
    std::size_t unspokenChars = 0; // 仅在没有任何工具调用时填写：模型直接写出、未发出的文字的码点数
};

// group_say / leave_topic 的参数串经 parseToolArguments 解析（容忍网关拼接的连续对象），
// 完全无法解析时抛出异常（同 parseToolArguments）
EngagementTurn parseEngagementTurn(const ChatResponse &response);

#endif // ENGAGEMENT_TURN_H

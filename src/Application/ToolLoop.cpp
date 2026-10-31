#include "ToolLoop.h"
#include "../Log/Log.h"
#include "../Tool/ToolArgumentParser.h"
#include "../Tool/ToolRegistry.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace
{
bool isLocalTool(const ToolLoopHooks &hooks, const std::string &name)
{
    return hooks.runLocalTool &&
           std::find(hooks.localToolNames.begin(), hooks.localToolNames.end(), name) !=
               hooks.localToolNames.end();
}

// 本循环能处理的调用：本地工具，或开放给本循环的注册表工具
bool isKnownTool(const ToolLoopHooks &hooks, const ToolRegistry *tools, const std::string &name)
{
    if (isLocalTool(hooks, name))
        return true;
    if (hooks.offered)
        return hooks.offered(name);
    return tools != nullptr && tools->find(name) != nullptr;
}

bool wantsTools(const ChatResponse &response, const ToolLoopHooks &hooks)
{
    if (response.code != 200 || response.tool_calls.empty())
        return false;
    return !hooks.requireToolCallsFinishReason || response.finish_reason == "tool_calls";
}

void logDone(const ToolLoopLogText &log, const std::string &toolName, const ToolResult &result)
{
    LOG_INFO(log.toolLabel + toolName + " 完成，回灌 " + std::to_string(result.model_content.size()) +
             " 字符" + log.doneSuffix);
}

// 单个调用走到了哪条路径（决定 AfterRegistryCall 的"完成"日志是否输出）
enum class CallPath
{
    Local,        // 本地工具（调用方自行记日志）
    NotOffered,   // 未开放给本循环
    Unregistered, // 开放但注册表里找不到
    Denied,       // 权限不足
    Executed,     // 注册表工具正常返回
    Threw,        // 注册表工具抛异常
};

ToolResult executeCall(const ResponseToolCall &call, const ToolRegistry *tools,
                       const ToolContext &context, const ToolLoopHooks &hooks, CallPath &path)
{
    ToolResult result;
    if (isLocalTool(hooks, call.name))
    {
        path = CallPath::Local;
        return hooks.runLocalTool(call);
    }

    if (hooks.offered && !hooks.offered(call.name))
    {
        path = CallPath::NotOffered;
        result.model_content = "错误：未知工具 " + call.name;
        LOG_WARNING(hooks.log.caller + "调用了未开放的工具：" + call.name + hooks.log.notOfferedSuffix);
        return result;
    }

    Tool *tool = tools != nullptr ? tools->find(call.name) : nullptr;
    if (tool == nullptr)
    {
        path = CallPath::Unregistered;
        result.model_content = "错误：未知工具 " + call.name;
        LOG_WARNING(hooks.log.caller + "调用了未注册的工具：" + call.name);
        return result;
    }

    if (hooks.permitted && !hooks.permitted(*tool))
    {
        path = CallPath::Denied;
        return {"权限不足：该操作仅限管理员。", {}, {}, true};
    }

    try
    {
        result = tool->execute(hooks.normalizeExecutionArguments
                                   ? normalizeToolArguments(call.arguments)
                                   : call.arguments,
                               context);
        path = CallPath::Executed;
        if (hooks.log.doneLog == ToolDoneLog::OnSuccess)
            logDone(hooks.log, call.name, result);
    }
    catch (const std::exception &e)
    {
        path = CallPath::Threw;
        result = ToolResult{};
        result.model_content = "错误：工具执行失败 " + std::string(e.what());
        LOG_ERROR(hooks.log.toolLabel + call.name + " 执行异常：" + std::string(e.what()));
    }
    return result;
}

bool logsDoneAfterCall(CallPath path, const ToolLoopLogText &log)
{
    return log.doneLog == ToolDoneLog::AfterRegistryCall &&
           (path == CallPath::Executed || path == CallPath::Unregistered || path == CallPath::Threw);
}
} // namespace

ToolLoopResult runToolLoop(std::vector<ChatMessage> &history, ChatResponse first,
                           const ToolRegistry *tools, const ToolContext &context,
                           const ToolLoopHooks &hooks)
{
    ToolLoopResult result;
    result.response = std::move(first);
    bool wrapUpAttempted = false;

    while (true)
    {
        const ChatResponse &response = result.response;
        if (response.cancelled)
        {
            result.stop = ToolLoopStop::Cancelled;
            return result;
        }
        if (!wantsTools(response, hooks))
        {
            result.stop = ToolLoopStop::Completed;
            return result;
        }

        if (hooks.stopsBeforeExecution)
        {
            for (const ResponseToolCall &call : response.tool_calls)
            {
                if (hooks.stopsBeforeExecution(call))
                {
                    result.stop = ToolLoopStop::StoppedBeforeExecution;
                    return result;
                }
            }
        }
        if (hooks.stopWhenNoKnownTool)
        {
            const bool anyKnown = std::any_of(
                response.tool_calls.begin(), response.tool_calls.end(),
                [&](const ResponseToolCall &call) { return isKnownTool(hooks, tools, call.name); });
            if (!anyKnown)
            {
                result.stop = ToolLoopStop::NoKnownTool;
                return result;
            }
        }

        if (result.rounds >= hooks.maxRounds)
        {
            if (!hooks.onRoundLimit || wrapUpAttempted)
            {
                result.stop = ToolLoopStop::RoundLimit;
                return result;
            }
            wrapUpAttempted = true;
            result.response = hooks.onRoundLimit(history);
            continue;
        }
        ++result.rounds;

        // 一条 assistant 消息带全部 tool_calls。部分网关会返回拼接损坏的 arguments 文本，
        // 原样回灌会让网关无法重建 tool_use 块，导致工具结果被整体丢弃、模型盲重试，
        // 因此回灌前先归一化为合法 JSON
        ChatMessage assistantMsg;
        assistantMsg.role = "assistant";
        for (const ResponseToolCall &call : response.tool_calls)
            assistantMsg.tool_calls.push_back({call.id, call.name, normalizeToolArguments(call.arguments)});
        history.push_back(std::move(assistantMsg));

        // 逐个执行，结果作为 role=="tool" 消息回灌
        bool terminal = false;
        for (const ResponseToolCall &call : response.tool_calls)
        {
            CallPath path = CallPath::Local;
            const ToolResult toolResult = executeCall(call, tools, context, hooks, path);
            if (toolResult.cancelled)
            {
                result.stop = ToolLoopStop::ToolCancelled;
                return result;
            }
            if (!toolResult.outbound_messages.empty())
            {
                if (hooks.onOutbound)
                    hooks.onOutbound(call.name, toolResult.outbound_messages);
                result.outbound_messages.insert(result.outbound_messages.end(),
                                                toolResult.outbound_messages.begin(),
                                                toolResult.outbound_messages.end());
            }
            if (logsDoneAfterCall(path, hooks.log))
                logDone(hooks.log, call.name, toolResult);

            ChatMessage toolMsg;
            toolMsg.role = "tool";
            toolMsg.tool_call_id = call.id;
            toolMsg.content = toolResult.model_content;
            history.push_back(std::move(toolMsg));
            if (!toolResult.context_content.empty())
                result.context_annotations.push_back(toolResult.context_content);

            if (hooks.honourTerminal)
            {
                if (!toolResult.terminal && !toolResult.outbound_messages.empty())
                    LOG_WARNING(hooks.log.toolLabel + call.name +
                                " 产生了出站消息但未声明 terminal，已自动终止本轮工具循环");
                if (terminatesToolRound(toolResult))
                {
                    result.terminal_content = toolResult.model_content;
                    result.suppress_text_reply = toolResult.suppress_text_reply;
                    terminal = true;
                    break;
                }
            }
        }
        if (terminal)
        {
            result.stop = ToolLoopStop::Terminal;
            return result;
        }

        // 带着加长的 history 再次请求
        result.response = hooks.request(history);
    }
}

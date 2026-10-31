#include <gtest/gtest.h>

#include "Application/ToolLoop.h"
#include "Tool/ToolArgumentParser.h"
#include "Tool/ToolRegistry.h"

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

// 工具循环的唯一实现（ADR-022）：对话路径与话题会话轮共用。这里用假请求函数 + 假工具
// 钉死循环机制本身；两个调用方的端到端回归分别由 EngagementTests 与现有对话测试兜底
namespace
{
class FakeTool : public Tool
{
public:
    using Behaviour = std::function<ToolResult(const std::string &args, const ToolContext &ctx)>;

    FakeTool(std::string name, Behaviour behaviour, bool admin = false)
        : toolName(std::move(name)), behaviour(std::move(behaviour)), admin(admin) {}

    std::string name() const override { return toolName; }
    std::string description() const override { return "fake"; }
    std::string parametersSchema() const override { return R"({"type":"object","properties":{}})"; }
    bool requiresAdmin() const override { return admin; }

    ToolResult execute(const std::string &args, const ToolContext &ctx) override
    {
        receivedArgs.push_back(args);
        receivedContexts.push_back(ctx);
        return behaviour(args, ctx);
    }

    std::vector<std::string> receivedArgs;
    std::vector<ToolContext> receivedContexts;

private:
    std::string toolName;
    Behaviour behaviour;
    bool admin;
};

ToolResult textResult(const std::string &content)
{
    ToolResult result;
    result.model_content = content;
    return result;
}

ResponseToolCall call(const std::string &id, const std::string &name,
                      const std::string &arguments = "{}")
{
    return {id, name, arguments};
}

ChatResponse toolResponse(std::vector<ResponseToolCall> calls,
                          const std::string &finishReason = "tool_calls")
{
    ChatResponse response;
    response.code = 200;
    response.finish_reason = finishReason;
    response.tool_calls = std::move(calls);
    return response;
}

ChatResponse textResponse(const std::string &content)
{
    ChatResponse response;
    response.code = 200;
    response.finish_reason = "stop";
    response.content = content;
    return response;
}

// 按顺序吐出预设响应的假请求函数，记录每次请求时的 history 长度
class ScriptedModel
{
public:
    explicit ScriptedModel(std::vector<ChatResponse> script) : script(std::move(script)) {}

    std::function<ChatResponse(const std::vector<ChatMessage> &)> fn()
    {
        return [this](const std::vector<ChatMessage> &history)
        {
            historySizes.push_back(history.size());
            if (next >= script.size())
                return textResponse("脚本已用完");
            return script[next++];
        };
    }

    std::size_t calls() const { return historySizes.size(); }
    std::vector<std::size_t> historySizes;

private:
    std::vector<ChatResponse> script;
    std::size_t next = 0;
};

class ToolLoopTest : public ::testing::Test
{
protected:
    FakeTool *addTool(const std::string &name, FakeTool::Behaviour behaviour, bool admin = false)
    {
        auto tool = std::make_unique<FakeTool>(name, std::move(behaviour), admin);
        FakeTool *raw = tool.get();
        registry.registerTool(std::move(tool));
        return raw;
    }

    std::vector<ChatMessage> freshHistory() const { return {{"user", "你好"}}; }

    ToolRegistry registry;
    const ToolContext context{42, 7, "原话", 9};
};
} // namespace

TEST_F(ToolLoopTest, SingleCallAppendsAssistantAndToolMessageThenReRequests)
{
    FakeTool *echo = addTool("echo", [](const std::string &, const ToolContext &)
                             { return textResult("回声"); });
    ScriptedModel model({textResponse("最终回答")});
    ToolLoopHooks hooks;
    hooks.request = model.fn();

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result =
        runToolLoop(history, toolResponse({call("c1", "echo", R"({"q":"x"})")}), &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::Completed);
    EXPECT_EQ(result.response.content, "最终回答");
    EXPECT_EQ(result.rounds, 1);
    ASSERT_EQ(echo->receivedArgs.size(), 1u);
    EXPECT_EQ(echo->receivedArgs[0], R"({"q":"x"})") << "对话路径默认原样交给工具";
    EXPECT_EQ(echo->receivedContexts[0].user_id, 42u);
    EXPECT_EQ(echo->receivedContexts[0].user_message_id, 7);
    EXPECT_EQ(echo->receivedContexts[0].user_text, "原话");
    EXPECT_EQ(echo->receivedContexts[0].shared_scope_id, 9u);

    ASSERT_EQ(history.size(), 3u);
    EXPECT_EQ(history[1].role, "assistant");
    ASSERT_EQ(history[1].tool_calls.size(), 1u);
    EXPECT_EQ(history[1].tool_calls[0].id, "c1");
    EXPECT_EQ(history[1].tool_calls[0].name, "echo");
    EXPECT_EQ(history[1].tool_calls[0].arguments, normalizeToolArguments(R"({"q":"x"})"));
    EXPECT_EQ(history[2].role, "tool");
    EXPECT_EQ(history[2].tool_call_id, "c1");
    EXPECT_EQ(history[2].content, "回声");
    ASSERT_EQ(model.calls(), 1u);
    EXPECT_EQ(model.historySizes[0], 3u) << "再请求时带着加长的 history";
}

TEST_F(ToolLoopTest, MultipleCallsInOneResponseShareOneAssistantMessage)
{
    addTool("a", [](const std::string &, const ToolContext &) { return textResult("A"); });
    addTool("b", [](const std::string &, const ToolContext &) { return textResult("B"); });
    ScriptedModel model({textResponse("好")});
    ToolLoopHooks hooks;
    hooks.request = model.fn();

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result = runToolLoop(
        history, toolResponse({call("1", "a"), call("2", "b")}), &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::Completed);
    ASSERT_EQ(history.size(), 4u);
    ASSERT_EQ(history[1].tool_calls.size(), 2u);
    EXPECT_EQ(history[2].tool_call_id, "1");
    EXPECT_EQ(history[2].content, "A");
    EXPECT_EQ(history[3].tool_call_id, "2");
    EXPECT_EQ(history[3].content, "B");
    EXPECT_EQ(model.calls(), 1u);
}

TEST_F(ToolLoopTest, UnknownToolGetsErrorTextAndLoopContinues)
{
    ScriptedModel model({textResponse("好")});
    ToolLoopHooks hooks;
    hooks.request = model.fn();

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result =
        runToolLoop(history, toolResponse({call("1", "nope")}), &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::Completed);
    ASSERT_EQ(history.size(), 3u);
    EXPECT_EQ(history[2].content, "错误：未知工具 nope");
    EXPECT_EQ(model.calls(), 1u);
}

TEST_F(ToolLoopTest, NotPermittedToolIsDeniedAsTerminalWithoutExecuting)
{
    FakeTool *admin = addTool("admin_only", [](const std::string &, const ToolContext &)
                              { return textResult("不该执行"); }, true);
    ScriptedModel model({textResponse("不该请求")});
    ToolLoopHooks hooks;
    hooks.request = model.fn();
    hooks.permitted = [](const Tool &tool) { return !tool.requiresAdmin(); };

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result =
        runToolLoop(history, toolResponse({call("1", "admin_only")}), &registry, context, hooks);

    EXPECT_TRUE(admin->receivedArgs.empty());
    EXPECT_EQ(result.stop, ToolLoopStop::Terminal);
    EXPECT_EQ(result.terminal_content, "权限不足：该操作仅限管理员。");
    ASSERT_EQ(history.size(), 3u);
    EXPECT_EQ(history[2].content, "权限不足：该操作仅限管理员。");
    EXPECT_EQ(model.calls(), 0u);
}

TEST_F(ToolLoopTest, ToolExceptionBecomesErrorTextAndLoopContinues)
{
    addTool("boom", [](const std::string &, const ToolContext &) -> ToolResult
            { throw std::runtime_error("炸了"); });
    ScriptedModel model({textResponse("好")});
    ToolLoopHooks hooks;
    hooks.request = model.fn();

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result =
        runToolLoop(history, toolResponse({call("1", "boom")}), &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::Completed);
    ASSERT_EQ(history.size(), 3u);
    EXPECT_EQ(history[2].content, "错误：工具执行失败 炸了");
    EXPECT_EQ(model.calls(), 1u);
}

TEST_F(ToolLoopTest, CancelledModelResponseStopsImmediately)
{
    ScriptedModel model(std::vector<ChatResponse>{});
    ToolLoopHooks hooks;
    hooks.request = model.fn();

    ChatResponse cancelled = toolResponse({call("1", "echo")});
    cancelled.cancelled = true;
    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result = runToolLoop(history, cancelled, &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::Cancelled);
    EXPECT_EQ(history.size(), 1u);
    EXPECT_EQ(model.calls(), 0u);

    // 再请求时被取消同样立即停止
    addTool("echo", [](const std::string &, const ToolContext &) { return textResult("x"); });
    ChatResponse later;
    later.cancelled = true;
    ScriptedModel cancelling({later});
    hooks.request = cancelling.fn();
    std::vector<ChatMessage> second = freshHistory();
    const ToolLoopResult secondResult =
        runToolLoop(second, toolResponse({call("1", "echo")}), &registry, context, hooks);
    EXPECT_EQ(secondResult.stop, ToolLoopStop::Cancelled);
    EXPECT_EQ(cancelling.calls(), 1u);
}

TEST_F(ToolLoopTest, CancelledToolResultStopsWithoutAppendingOrRequesting)
{
    FakeTool *second = addTool("second", [](const std::string &, const ToolContext &)
                               { return textResult("不该执行"); });
    addTool("halt", [](const std::string &, const ToolContext &)
            {
                ToolResult result;
                result.cancelled = true;
                return result;
            });
    ScriptedModel model({textResponse("不该请求")});
    ToolLoopHooks hooks;
    hooks.request = model.fn();

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result = runToolLoop(
        history, toolResponse({call("1", "halt"), call("2", "second")}), &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::ToolCancelled);
    EXPECT_TRUE(second->receivedArgs.empty());
    ASSERT_EQ(history.size(), 2u) << "只有 assistant 消息，取消的调用不追加 tool 消息";
    EXPECT_EQ(model.calls(), 0u);
}

TEST_F(ToolLoopTest, TerminalToolStopsLoopAndSkipsLaterCalls)
{
    addTool("finish", [](const std::string &, const ToolContext &)
            {
                ToolResult result;
                result.model_content = "图已发出";
                result.terminal = true;
                result.suppress_text_reply = true;
                result.context_content = "[注记]";
                return result;
            });
    FakeTool *after = addTool("after", [](const std::string &, const ToolContext &)
                              { return textResult("不该执行"); });
    ScriptedModel model({textResponse("不该请求")});
    ToolLoopHooks hooks;
    hooks.request = model.fn();

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result = runToolLoop(
        history, toolResponse({call("1", "finish"), call("2", "after")}), &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::Terminal);
    EXPECT_EQ(result.terminal_content, "图已发出");
    EXPECT_TRUE(result.suppress_text_reply);
    ASSERT_EQ(result.context_annotations.size(), 1u);
    EXPECT_EQ(result.context_annotations[0], "[注记]");
    EXPECT_TRUE(after->receivedArgs.empty());
    EXPECT_EQ(history.size(), 3u);
    EXPECT_EQ(model.calls(), 0u);
}

TEST_F(ToolLoopTest, OutboundWithoutTerminalFlagStillEndsTheRoundWhenHonoured)
{
    addTool("send", [](const std::string &, const ToolContext &)
            {
                ToolResult result;
                result.model_content = "已发送";
                result.outbound_messages.push_back(TextMessage{"图"});
                return result;
            });
    ScriptedModel model({textResponse("不该请求")});
    ToolLoopHooks hooks;
    hooks.request = model.fn();

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result =
        runToolLoop(history, toolResponse({call("1", "send")}), &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::Terminal);
    ASSERT_EQ(result.outbound_messages.size(), 1u);
    EXPECT_EQ(model.calls(), 0u);
}

TEST_F(ToolLoopTest, OutboundIsForwardedImmediatelyAndCollected)
{
    addTool("send", [](const std::string &, const ToolContext &)
            {
                ToolResult result;
                result.model_content = "已发送";
                result.terminal = true;
                result.outbound_messages.push_back(TextMessage{"第一张"});
                result.outbound_messages.push_back(TextMessage{"第二张"});
                return result;
            });
    ScriptedModel model({textResponse("发好了")});
    std::vector<std::string> forwardedTools;
    std::size_t forwardedCount = 0;
    ToolLoopHooks hooks;
    hooks.request = model.fn();
    hooks.honourTerminal = false; // 会话轮：图当场发出后继续定案
    hooks.onOutbound = [&](const std::string &toolName, const std::vector<OutboundMessage> &messages)
    {
        forwardedTools.push_back(toolName);
        forwardedCount += messages.size();
    };

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result =
        runToolLoop(history, toolResponse({call("1", "send")}), &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::Completed);
    EXPECT_EQ(result.response.content, "发好了");
    ASSERT_EQ(forwardedTools.size(), 1u);
    EXPECT_EQ(forwardedTools[0], "send");
    EXPECT_EQ(forwardedCount, 2u);
    ASSERT_EQ(result.outbound_messages.size(), 2u);
    EXPECT_EQ(std::get<TextMessage>(result.outbound_messages[0]).content, "第一张");
    EXPECT_EQ(model.calls(), 1u);
}

TEST_F(ToolLoopTest, RoundLimitWithoutWrapUpStopsAfterMaxRounds)
{
    addTool("again", [](const std::string &, const ToolContext &) { return textResult("再来"); });
    ScriptedModel model({toolResponse({call("2", "again")}), toolResponse({call("3", "again")}),
                         toolResponse({call("4", "again")})});
    ToolLoopHooks hooks;
    hooks.request = model.fn();
    hooks.maxRounds = 2;

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result =
        runToolLoop(history, toolResponse({call("1", "again")}), &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::RoundLimit);
    EXPECT_EQ(result.rounds, 2);
    EXPECT_EQ(model.calls(), 2u);
    EXPECT_FALSE(result.response.tool_calls.empty()) << "最后一次响应仍在要工具";
    EXPECT_EQ(history.size(), 5u);
}

TEST_F(ToolLoopTest, RoundLimitWrapUpRunsOnceAndCanComplete)
{
    addTool("again", [](const std::string &, const ToolContext &) { return textResult("再来"); });

    // 收尾请求给出文字 → Completed
    {
        ScriptedModel model({toolResponse({call("2", "again")})});
        int wrapUps = 0;
        ToolLoopHooks hooks;
        hooks.request = model.fn();
        hooks.maxRounds = 1;
        hooks.onRoundLimit = [&](std::vector<ChatMessage> &history)
        {
            ++wrapUps;
            history.push_back({"user", "[系统注] 收尾"});
            return textResponse("基于已有证据作答");
        };
        std::vector<ChatMessage> history = freshHistory();
        const ToolLoopResult result =
            runToolLoop(history, toolResponse({call("1", "again")}), &registry, context, hooks);
        EXPECT_EQ(result.stop, ToolLoopStop::Completed);
        EXPECT_EQ(result.response.content, "基于已有证据作答");
        EXPECT_EQ(wrapUps, 1);
        EXPECT_EQ(result.rounds, 1);
        ASSERT_EQ(history.size(), 4u);
        EXPECT_EQ(history.back().content, "[系统注] 收尾");
    }

    // 收尾请求仍要工具 → RoundLimit，收尾至多一次
    {
        ScriptedModel model({toolResponse({call("2", "again")})});
        int wrapUps = 0;
        ToolLoopHooks hooks;
        hooks.request = model.fn();
        hooks.maxRounds = 1;
        hooks.onRoundLimit = [&](std::vector<ChatMessage> &)
        {
            ++wrapUps;
            return toolResponse({call("3", "again")});
        };
        std::vector<ChatMessage> history = freshHistory();
        const ToolLoopResult result =
            runToolLoop(history, toolResponse({call("1", "again")}), &registry, context, hooks);
        EXPECT_EQ(result.stop, ToolLoopStop::RoundLimit);
        EXPECT_EQ(wrapUps, 1);
        EXPECT_EQ(result.rounds, 1);
    }
}

TEST_F(ToolLoopTest, StopsBeforeExecutingAnythingWhenDefinitiveCallPresent)
{
    FakeTool *lookup = addTool("lookup", [](const std::string &, const ToolContext &)
                               { return textResult("不该执行"); });
    ScriptedModel model(std::vector<ChatResponse>{});
    ToolLoopHooks hooks;
    hooks.request = model.fn();
    hooks.requireToolCallsFinishReason = false;
    hooks.stopsBeforeExecution = [](const ResponseToolCall &c) { return c.name == "group_say"; };

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result = runToolLoop(
        history, toolResponse({call("1", "lookup"), call("2", "group_say", R"({"text":"嗨"})")}, "stop"),
        &registry, context, hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::StoppedBeforeExecution);
    EXPECT_TRUE(lookup->receivedArgs.empty());
    EXPECT_EQ(history.size(), 1u);
    EXPECT_EQ(model.calls(), 0u);
    ASSERT_EQ(result.response.tool_calls.size(), 2u) << "调用方据最后响应解读定案动作";
}

TEST_F(ToolLoopTest, LocalToolRunsThroughHookWithoutRegistry)
{
    std::vector<std::string> localCalls;
    ScriptedModel model({textResponse("好")});
    ToolLoopHooks hooks;
    hooks.request = model.fn();
    hooks.localToolNames = {"recall_context"};
    hooks.runLocalTool = [&](const ResponseToolCall &c)
    {
        localCalls.push_back(c.arguments);
        return textResult("召回结果");
    };

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result = runToolLoop(
        history, toolResponse({call("1", "recall_context", R"({"keywords":["a"]})")}), nullptr, context,
        hooks);

    EXPECT_EQ(result.stop, ToolLoopStop::Completed);
    ASSERT_EQ(localCalls.size(), 1u);
    EXPECT_EQ(localCalls[0], R"({"keywords":["a"]})");
    ASSERT_EQ(history.size(), 3u);
    EXPECT_EQ(history[2].content, "召回结果");
}

TEST_F(ToolLoopTest, OfferedFilterAndNoKnownToolStop)
{
    FakeTool *openTool = addTool("open", [](const std::string &, const ToolContext &)
                             { return textResult("开放的"); });
    FakeTool *hidden = addTool("hidden", [](const std::string &, const ToolContext &)
                               { return textResult("不该执行"); });
    ToolLoopHooks hooks;
    hooks.requireToolCallsFinishReason = false;
    hooks.offered = [](const std::string &name) { return name == "open"; };
    hooks.stopWhenNoKnownTool = true;

    // 只有未开放/未注册工具：不执行、不再请求
    {
        ScriptedModel model(std::vector<ChatResponse>{});
        hooks.request = model.fn();
        std::vector<ChatMessage> history = freshHistory();
        const ToolLoopResult result = runToolLoop(
            history, toolResponse({call("1", "hidden"), call("2", "ghost")}), &registry, context, hooks);
        EXPECT_EQ(result.stop, ToolLoopStop::NoKnownTool);
        EXPECT_EQ(history.size(), 1u);
        EXPECT_EQ(model.calls(), 0u);
    }

    // 混合：开放的执行，未开放的回灌"未知工具"
    {
        ScriptedModel model({textResponse("好")});
        hooks.request = model.fn();
        std::vector<ChatMessage> history = freshHistory();
        const ToolLoopResult result = runToolLoop(
            history, toolResponse({call("1", "open"), call("2", "hidden")}), &registry, context, hooks);
        EXPECT_EQ(result.stop, ToolLoopStop::Completed);
        ASSERT_EQ(history.size(), 4u);
        EXPECT_EQ(history[2].content, "开放的");
        EXPECT_EQ(history[3].content, "错误：未知工具 hidden");
        EXPECT_EQ(openTool->receivedArgs.size(), 1u);
        EXPECT_TRUE(hidden->receivedArgs.empty());
    }
}

TEST_F(ToolLoopTest, FinishReasonRequirementAndArgumentNormalization)
{
    FakeTool *echo = addTool("echo", [](const std::string &, const ToolContext &)
                             { return textResult("x"); });
    const std::string broken = R"({"a":1}{"b":2})"; // 网关拼接损坏的参数

    // 对话路径：finish_reason 不是 tool_calls 时不执行
    {
        ScriptedModel model(std::vector<ChatResponse>{});
        ToolLoopHooks hooks;
        hooks.request = model.fn();
        std::vector<ChatMessage> history = freshHistory();
        const ToolLoopResult result =
            runToolLoop(history, toolResponse({call("1", "echo")}, "stop"), &registry, context, hooks);
        EXPECT_EQ(result.stop, ToolLoopStop::Completed);
        EXPECT_TRUE(echo->receivedArgs.empty());
        EXPECT_EQ(history.size(), 1u);
    }

    // 会话轮：有 tool_calls 即执行，参数先归一化
    {
        ScriptedModel model({textResponse("好")});
        ToolLoopHooks hooks;
        hooks.request = model.fn();
        hooks.requireToolCallsFinishReason = false;
        hooks.normalizeExecutionArguments = true;
        std::vector<ChatMessage> history = freshHistory();
        const ToolLoopResult result = runToolLoop(
            history, toolResponse({call("1", "echo", broken)}, "stop"), &registry, context, hooks);
        EXPECT_EQ(result.stop, ToolLoopStop::Completed);
        ASSERT_EQ(echo->receivedArgs.size(), 1u);
        EXPECT_EQ(echo->receivedArgs[0], normalizeToolArguments(broken));
        EXPECT_EQ(history[1].tool_calls[0].arguments, normalizeToolArguments(broken));
    }
}

TEST_F(ToolLoopTest, FailedModelResponseIsReturnedForCallerToInterpret)
{
    ScriptedModel model(std::vector<ChatResponse>{});
    ToolLoopHooks hooks;
    hooks.request = model.fn();
    ChatResponse failed = toolResponse({call("1", "echo")});
    failed.code = 500;

    std::vector<ChatMessage> history = freshHistory();
    const ToolLoopResult result = runToolLoop(history, failed, &registry, context, hooks);
    EXPECT_EQ(result.stop, ToolLoopStop::Completed);
    EXPECT_EQ(result.response.code, 500);
    EXPECT_EQ(history.size(), 1u);
}

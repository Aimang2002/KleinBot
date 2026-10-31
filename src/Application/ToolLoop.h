#ifndef TOOL_LOOP_H
#define TOOL_LOOP_H

/*
 * 工具循环的唯一实现（ADR-022 机制复用）：ChatService::reply（一对一对话）与
 * EngagementService::runTurn（群话题会话轮）共用。
 *
 * 一趟 = 把模型的 tool_calls（参数归一化后）作为一条 assistant 消息追加进 history →
 * 逐个执行（本地工具 / 注册表工具 / 未开放 / 未注册 / 无权限，try/catch 兜底）→
 * 每个调用追加一条 role=tool 消息 → 带着加长的 history 再请求一次。
 * 场景差异全部经 ToolLoopHooks 表达，循环本身不认识任何工具名。
 *
 * 不变量（两个调用方原有语义）：
 *   - 模型响应 cancelled 或工具结果 cancelled → 立即停止（不再追加、不再请求）；
 *   - honourTerminal 时，terminal 或带出站消息的工具结果结束整个循环，同一回复里
 *     后面的调用不执行（对话路径：生图/发图/权限拒绝即终态）；
 *   - 工具的出站消息全部收集进结果，并可经 onOutbound 当场转交（会话轮：立即投递）；
 *   - context_content 收集为注记（对话路径落库时附在助手回复后）。
 */
#include "../Port/ChatRequest.h"
#include "../Port/ChatResponse.h"
#include "../Port/OutboundMessage.h"
#include "../Tool/Tool.h"
#include "../Tool/ToolContext.h"

#include <functional>
#include <string>
#include <vector>

class ToolRegistry;

enum class ToolLoopStop
{
    Completed,              // 最新响应不再请求工具（或响应失败 code≠200），由调用方解读 response
    Cancelled,              // 某次模型请求被取消（response.cancelled）
    ToolCancelled,          // 某个工具结果报告取消（停机时的在途 HTTP）
    Terminal,               // honourTerminal 下终态工具结果结束了循环
    StoppedBeforeExecution, // stopsBeforeExecution 命中：本回复里的调用一个都没执行
    NoKnownTool,            // stopWhenNoKnownTool 下本回复只含未知/未开放工具
    RoundLimit,             // 轮次上限：无 onRoundLimit，或收尾请求仍要工具
};

struct ToolLoopResult
{
    ToolLoopStop stop = ToolLoopStop::Completed;
    ChatResponse response;                          // 最后一次模型响应
    std::vector<OutboundMessage> outbound_messages; // 全部工具出站消息（按产生顺序）
    std::vector<std::string> context_annotations;   // 非空的 ToolResult::context_content
    std::string terminal_content;                   // Terminal 时该工具的 model_content
    bool suppress_text_reply = false;               // Terminal 时该工具的 suppress_text_reply
    int rounds = 0;                                 // 实际执行过的工具轮数
};

// "<toolLabel>X 完成，回灌 N 字符<doneSuffix>"日志的时机：保持两个调用方原有日志
enum class ToolDoneLog
{
    OnSuccess,         // 对话路径：注册表工具正常返回后立即输出（先于取消检查）
    AfterRegistryCall, // 会话轮：注册表调用（含未注册、执行异常）处理完、出站投递之后输出
};

// 日志措辞：保持两个调用方原有日志文本
struct ToolLoopLogText
{
    std::string caller = "模型";     // "<caller>调用了未注册的工具：X" / "<caller>调用了未开放的工具：X<notOfferedSuffix>"
    std::string toolLabel = "工具 "; // "<toolLabel>X 完成，回灌 N 字符<doneSuffix>" / "<toolLabel>X 执行异常：…"
    std::string doneSuffix;          // 会话轮："：群 123"
    std::string notOfferedSuffix;    // 会话轮："（群 123）"
    ToolDoneLog doneLog = ToolDoneLog::OnSuccess;
};

struct ToolLoopHooks
{
    // 已执行轮数达到上限后模型仍要工具 → onRoundLimit（若有，至多一次）否则 RoundLimit
    int maxRounds = 5;
    // 发送当前 history（循环已把 assistant/tool 消息追加进去）；必填
    std::function<ChatResponse(const std::vector<ChatMessage> &history)> request;
    // true（对话路径）：只有 finish_reason=="tool_calls" 才执行工具；false（会话轮）：有 tool_calls 即执行
    bool requireToolCallsFinishReason = true;
    // 命中任一调用即在执行任何调用之前停止（会话轮：group_say/leave_topic 是定案动作）
    std::function<bool(const ResponseToolCall &call)> stopsBeforeExecution;
    // 本地工具（不在注册表里、由调用方就地执行，如会话轮 recall_context 带每会话预算）
    std::vector<std::string> localToolNames;
    std::function<ToolResult(const ResponseToolCall &call)> runLocalTool;
    // 注册表工具是否开放给本循环；空 = 注册表里的全部开放（会话轮：setTools 过滤后的名单）
    std::function<bool(const std::string &name)> offered;
    // 本回复只含未知/未开放工具时停止（会话轮：按本拍沉默处理，不回灌"未知工具"再问）
    bool stopWhenNoKnownTool = false;
    // 权限：空 = 全部允许；false → 回灌"权限不足：该操作仅限管理员。"（终态）
    std::function<bool(const Tool &tool)> permitted;
    // true：执行前把参数归一化为合法 JSON（会话轮）；false：原样交给工具（对话路径）
    bool normalizeExecutionArguments = false;
    // terminal / 带出站消息的结果是否结束循环（对话路径 true；会话轮 false：图当场发出后继续定案）
    bool honourTerminal = true;
    // 工具产生出站消息时当场回调（会话轮：入群缓冲 + 投递）；无论是否设置，结果里都会收集
    std::function<void(const std::string &toolName, const std::vector<OutboundMessage> &messages)> onOutbound;
    // 轮次上限的收尾：可改 history（如追加收尾注）后返回新响应；空 = 直接 RoundLimit
    std::function<ChatResponse(std::vector<ChatMessage> &history)> onRoundLimit;
    ToolLoopLogText log;
};

// 从 first（已拿到的第一次模型响应）开始跑工具循环；history 是本次请求的临时历史
// （调用方保证不持久化，ADR-007）。tools 可空（只有本地工具）
ToolLoopResult runToolLoop(std::vector<ChatMessage> &history, ChatResponse first,
                           const ToolRegistry *tools, const ToolContext &context,
                           const ToolLoopHooks &hooks);

#endif // TOOL_LOOP_H

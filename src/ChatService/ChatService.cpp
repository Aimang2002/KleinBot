#include "ChatService.h"
#include "../Application/CurrentImageRouting.h"
#include "../Application/RecallContextInjection.h"
#include "../Application/ReplyConstraints.h"
#include "../Application/ToolGuidance.h"
#include "../Application/ToolLoop.h"
#include "../Application/ToolPolicyFilter.h"
#include "../utils/LocalDate.h"
#include "../Memory/MemoryService.h"
#include "../Tool/ToolContext.h"
#include "../utils/Utils.hpp"
#include "../Log/Log.h"

ChatReply ChatService::reply(uint64_t user_id, const std::string &text,
                             std::optional<ChatImageContent> currentImage,
                             const std::string &situationNote,
                             const std::string &requestContext,
                             uint64_t sharedScopeId,
                             const ReplyConstraints *constraints)
{
    ChatReply resultReply;
    // 1. 增量追加用户这句（内存 + SQLite 同步落盘）：上下文对所有用户开放。
    // 只落用户原文；请求级临时上下文（群背景）不经过本函数持久化。
    int64_t userMessageId = this->userSession.appendMessage(user_id, "user", text);
    resultReply.user_message_id = userMessageId;

    // 2. 构造请求包（USS 负责裁切、查模型、拼超参数）
    auto bundleOpt = this->userSession.buildChatRequest(user_id);
    if (!bundleOpt)
    {
        resultReply.text = "系统提示：当前模型未注册，请管理员检查 ModelsName.json。";
        return resultReply;
    }
    auto &bundle = *bundleOpt;

    // 滚动摘要（2026-11-08）：锚点这一轮前移时，把移出窗口的原文在锁外交杂务模型
    // 合并进摘要；失败或未注入 worker 时保留旧摘要，被移出的那段丢弃（此前行为）
    std::string contextDigest = bundle.digest_before;
    if (!bundle.evicted_lines.empty() && this->digestWorker)
    {
        const std::string merged = mergeDigest(this->digestWorker, kPrivateChatDigestSystem,
                                               contextDigest, bundle.evicted_lines);
        if (!merged.empty())
        {
            contextDigest = merged;
            this->userSession.setContextDigest(user_id, contextDigest);
        }
        else
        {
            LOG_WARNING("私聊上下文摘要合并失败（杂务模型无返回）：保留旧摘要，移出窗口的原文丢弃");
        }
    }

    // 请求级临时上下文只修改请求副本，不写会话历史、不参与召回/提取
    if (!requestContext.empty() &&
        !appendTransientUserContext(bundle.request, requestContext))
        LOG_WARNING("请求上下文注入失败：请求中没有 user 消息");

    // 单轮情境注记紧跟服务契约：行为约束归 system，先于后续工具规则注入
    if (!situationNote.empty())
        bundle.request.system_prompt += situationNote;
    // 计划的分寸注记（R1.c）紧跟情境注记；无约束时不改动
    if (constraints != nullptr)
        bundle.request.system_prompt += planNote(*constraints);

    const CurrentImageRoute imageRoute = routeCurrentImage(
        bundle.request, bundle.model, bundle.model_name, std::move(currentImage));

    // 3. 塞入可用工具。使用规则由各工具自带（Tool::usageGuidance），经 composeToolGuidance
    // 按固定顺序组装：生产工具集下与此前手写的规则文本逐字节一致（system 前缀缓存稳定）
    // 计划收窄（R1.c）：工具表、使用规则与下方工具循环的开放名单用同一份过滤结果
    //（规则不提没开放的工具，免得诱导调用后被拒、白耗一趟工具轮）
    std::vector<std::string> offeredToolNames;
    if (constraints != nullptr)
    {
        offeredToolNames = filterToolNames(this->tools.names(), constraints->toolPolicy,
                                           constraints->allowedTools);
        bundle.request.tools.clear();
        for (const std::string &name : offeredToolNames)
            if (const std::optional<std::string> schema = this->tools.schemaFor(name))
                bundle.request.tools.push_back(*schema);
    }
    else
    {
        bundle.request.tools = this->tools.allSchemas();
    }
    bundle.request.system_prompt += composeToolGuidance(
        this->tools, constraints != nullptr ? offeredToolNames : this->tools.names(),
        GuidanceScene::Conversation, {currentLocalDateIso()});
    if (imageRoute == CurrentImageRoute::NativeMultimodal)
    {
        // 每轮互斥的路由指令必须注入消息尾部而不是 system：
        // system 位于前缀最前端，图片/文本交替会反复作废整个前缀缓存
        appendContextNote(bundle.request,
            "如果当前请求直接附带用户刚发送的图片，你可以查看它并直接回答，"
            "不需要为了查看这张当前图片调用 inspect_image。");
    }
    else if (imageRoute == CurrentImageRoute::ToolFallback)
    {
        appendContextNote(bundle.request,
            "当前用户消息中的图片没有直接提供给你；涉及其内容时必须调用 inspect_image。");
    }

    const std::string automaticRecall = this->memoryService.recallForMessage(
        user_id, text, userMessageId);
    if (!automaticRecall.empty())
    {
        if (!attachRecallContext(bundle.request, automaticRecall))
            LOG_WARNING("自动召回证据注入失败：请求中没有 user 消息");
    }
    // 早前对话摘要与召回证据同一注入点，后注入以位于召回块之前；只改请求副本
    if (!contextDigest.empty() && !attachDigestContext(bundle.request, contextDigest))
        LOG_WARNING("早前对话摘要注入失败：请求中没有 user 消息");

    // 4. 调用 LLM + 工具调用循环
    std::cout << "Send to model..." << std::endl;
    ChatResponse response = this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);
    if (response.cancelled)
        return resultReply;
    if (imageRoute == CurrentImageRoute::NativeMultimodal &&
        response.multimodal_unsupported)
    {
        const std::size_t removedImages = removeRequestImages(bundle.request);
        LOG_WARNING("当前主模型接口明确拒绝多模态内容，已移除 " +
                    std::to_string(removedImages) + " 张图片并降级到 inspect_image");
        appendContextNote(bundle.request,
            "更正：上方提到的当前图片未能直接附上，涉及其内容时必须调用 inspect_image。");
        response = this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);
        if (response.cancelled)
            return resultReply;
    }

    // 工具循环（Application/ToolLoop，与话题会话轮共用同一实现）：≤max_tool_rounds 轮，
    // 超限时追加收尾注再请求一次；terminal/出站消息结束循环
    ToolLoopHooks hooks;
    hooks.maxRounds = max_tool_rounds;
    hooks.request = [this, &bundle](const std::vector<ChatMessage> &)
    { return this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request); };
    hooks.permitted = [this, user_id](const Tool &tool)
    { return !tool.requiresAdmin() || user_id == managerId; };
    if (constraints != nullptr)
        hooks.offered = [&offeredToolNames](const std::string &name)
        {
            return std::find(offeredToolNames.begin(), offeredToolNames.end(), name) !=
                   offeredToolNames.end();
        };
    hooks.onRoundLimit = [this, &bundle](std::vector<ChatMessage> &history)
    {
        // 到达轮次上限不丢弃证据：让模型基于已有结果收尾作答
        LOG_WARNING("工具调用轮次超过上限，基于已有证据收尾作答");
        // 收尾指令作为 user 消息追加在尾部，tools 表原样保留：
        // 清空 tools 会改动前缀最前端，使该请求的全量历史缓存作废
        ChatMessage wrapUpNote;
        wrapUpNote.role = "user";
        wrapUpNote.content =
            "[系统注] 工具调用轮次已达上限，不得再调用任何工具。"
            "必须基于对话中已有的工具证据直接回答用户最初的问题，"
            "证据不足时明确说明具体缺了什么，不要请用户重试。";
        history.push_back(std::move(wrapUpNote));
        return this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);
    };
    ToolLoopResult loop = runToolLoop(bundle.request.history, std::move(response), &this->tools,
                                      ToolContext{user_id, userMessageId, text, sharedScopeId}, hooks);
    resultReply.outbound_messages = std::move(loop.outbound_messages);
    if (loop.stop == ToolLoopStop::Cancelled || loop.stop == ToolLoopStop::ToolCancelled)
        return resultReply;
    if (loop.stop == ToolLoopStop::RoundLimit)
    {
        LOG_WARNING("收尾请求仍然要求调用工具，强制结束");
        resultReply.text = "系统提示：处理超时，请重试。";
        return resultReply;
    }
    response = std::move(loop.response);
    const std::vector<std::string> &contextAnnotations = loop.context_annotations;
    const bool terminalToolResult = loop.stop == ToolLoopStop::Terminal;
    const bool suppressTerminalTextReply = loop.suppress_text_reply;
    const std::string &terminalToolContent = loop.terminal_content;

    // 5. 错误处理
    if (response.code != 200 && !terminalToolResult)
    {
        LOG_ERROR("LLM response error code: " + std::to_string(response.code));
        resultReply.text = response.error_message.empty() ? std::string("系统提示：模型无返回内容！") : "系统提示：" + response.error_message;
        return resultReply;
    }

    std::string LLM_content = terminalToolResult ? terminalToolContent : response.content;
    if (LLM_content.empty())
    {
        resultReply.text = "系统提示：模型无返回内容！";
        return resultReply;
    }

    // 5. 增量追加助手回复（内存 + SQLite 同步落盘），并入长期记忆提取队列
    {
        std::string persistedAssistantContent = LLM_content;
        for (const auto &annotation : contextAnnotations)
            persistedAssistantContent += "\n" + annotation;
        const int64_t assistantMessageId = this->userSession.appendMessage(
            user_id, "assistant", persistedAssistantContent);
        this->memoryService.enqueueTurn(
            user_id, text, LLM_content, userMessageId, assistantMessageId);
    }

    // 6. finish_reason 附加提示
    if (response.finish_reason == "length")
    {
        LOG_WARNING("该模型的回答长度超出了管理员设定的最大限制...");
        LLM_content += "\n模型已超出最大长度限制，回答可能不全。";
    }
    else if (response.finish_reason == "content_filter")
    {
        LOG_WARNING("该模型的回答内容被AI morality filter拦截...");
        LLM_content += "\n模型返回内容被AI morality filter拦截...";
    }

    std::cout << "\033[32m" << "Model response: " << "\033[0m" << LLM_content << std::endl;
    resultReply.text = suppressTerminalTextReply ? std::string() : LLM_content;
    return resultReply;
}

std::string ChatService::buildOnce(const std::string &systemPrompt, const std::string &userPrompt)
{
    return buildOnceWith(ModelEndpointOptions{}, systemPrompt, userPrompt);
}

std::string ChatService::buildOnceWith(const ModelEndpointOptions &worker,
                                       const std::string &systemPrompt,
                                       const std::string &userPrompt,
                                       long timeoutMs)
{
    ChatRequest request;
    request.temperature = 0.3; // 判断/压缩类任务要稳定，不要创造性
    request.timeout_ms = timeoutMs;
    request.system_prompt = systemPrompt;
    request.history.push_back({"user", userPrompt});

    ChatResponse response;
    if (worker.configured())
    {
        // 杂务专用端点（调用方已按注册表解析）：与 drawing/vision 相同的直连模式
        response = this->dock.RequestChat(worker, request);
    }
    else
    {
        const std::optional<ChatModel> model = this->models.find(chatConfig.defaultModel);
        if (!model)
        {
            LOG_ERROR("DEFAULT_MODEL 未在 ModelsName.json 注册：" + chatConfig.defaultModel);
            return {};
        }
        response = this->dock.RequestChat(*model, chatConfig.defaultModel, request);
    }
    if (response.cancelled)
        return {};
    if (response.code != 200)
    {
        LOG_ERROR("ChatService::buildOnceWith 调用 LLM 失败：" + std::to_string(response.code));
        return {};
    }
    return utils::trim(response.content);
}

ChatResponse ChatService::requestInCharacter(uint64_t personaSourceId, const std::string &systemNote,
                                             const std::vector<ChatMessage> &history,
                                             const std::vector<std::string> &toolSchemas)
{
    // 历史由调用方给定：不前移该用户的私聊锚点（trimWindow=false）
    auto bundleOpt = this->userSession.buildChatRequest(personaSourceId, false);
    if (!bundleOpt)
    {
        LOG_ERROR("ChatService::requestInCharacter 模型未注册，user_id=" +
                  std::to_string(personaSourceId));
        return {};
    }
    auto &bundle = *bundleOpt;

    // 人格与服务契约照常装配；历史与工具完全由调用方给定，不读会话镜像、
    // 不落库、不入长期记忆——话题会话是公共临时上下文，与任何真人会话无关
    bundle.request.history = history;
    bundle.request.tools = toolSchemas;
    bundle.request.system_prompt += systemNote;

    return this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);
}

std::string ChatService::replyInCharacter(uint64_t user_id, const std::string &prompt)
{
    // 单轮、历史清空：不前移私聊锚点，也不注入私聊摘要（trimWindow=false）
    auto bundleOpt = this->userSession.buildChatRequest(user_id, false);
    if (!bundleOpt)
    {
        LOG_ERROR("ChatService::replyInCharacter 模型未注册，user_id=" + std::to_string(user_id));
        return {};
    }
    auto &bundle = *bundleOpt;

    // 单轮：人格与服务契约照常装配，历史清空、不带工具。
    // prompt 必须手动塞进 history——buildChatRequest 只装配会话镜像，
    // 这里不落库，不 append 就没有任何 user 消息
    bundle.request.history.clear();
    bundle.request.history.push_back({"user", prompt});
    bundle.request.tools.clear();
    bundle.request.system_prompt +=
        "\n\n刚才那条不是正式对话，是一个需要你随口回应的小场景：你输出的这句话"
        "会被原样发到QQ里，所以直接说那句话本身——口语、自然，不要前缀、引号、"
        "解释或任何确认语。";

    ChatResponse response = this->dock.RequestChat(bundle.model, bundle.model_name, bundle.request);
    if (response.cancelled)
        return {};
    if (response.code != 200)
    {
        LOG_ERROR("ChatService::replyInCharacter 调用 LLM 失败：" + std::to_string(response.code));
        return {};
    }
    return utils::trim(response.content);
}

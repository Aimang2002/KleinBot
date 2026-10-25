#ifndef RECALL_CONTEXT_INJECTION_H
#define RECALL_CONTEXT_INJECTION_H

#include "../Port/ChatRequest.h"
#include <string>

bool attachRecallContext(ChatRequest &request, const std::string &evidence);

// 在最后一条 user 消息尾部追加当轮临时注记（不落库）。
// 每轮变化的内容必须放在消息流尾部而不是 system：system 位于请求前缀
// 最前端，任何逐字变化都会作废整个前缀缓存，而尾部变化最多损失一条消息
bool appendContextNote(ChatRequest &request, const std::string &note);

// 将只用于本次模型请求的临时上下文（如群聊背景）附在最后一条 user 消息尾部。
// 调用方必须只操作请求副本：该内容不写会话历史/数据库，也不参与记忆召回与提取。
// 与 appendContextNote 的区别是不加“[系统注]”前缀，保持群背景的原始块格式。
bool appendTransientUserContext(ChatRequest &request, const std::string &context);

#endif

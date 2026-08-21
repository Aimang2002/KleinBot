#ifndef RECALL_CONTEXT_INJECTION_H
#define RECALL_CONTEXT_INJECTION_H

#include "../Port/ChatRequest.h"
#include <string>

bool attachRecallContext(ChatRequest &request, const std::string &evidence);

// 在最后一条 user 消息尾部追加当轮临时注记（不落库）。
// 每轮变化的内容必须放在消息流尾部而不是 system：system 位于请求前缀
// 最前端，任何逐字变化都会作废整个前缀缓存，而尾部变化最多损失一条消息
bool appendContextNote(ChatRequest &request, const std::string &note);

#endif

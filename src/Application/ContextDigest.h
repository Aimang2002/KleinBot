#ifndef CONTEXT_DIGEST_H
#define CONTEXT_DIGEST_H

#include <functional>
#include <string>

// 滚动摘要合并（2026-11-08，群会话轮与私聊上下文共用一份实现）：
// 已有摘要 + 一段移出窗口的原文 → 杂务模型 → 新摘要。纯函数，不持锁、不落库

// 杂务模型 seam：(systemPrompt, userPrompt) → 输出；失败/超时返回空串。
// 签名与 EngagementService::Worker 相同，组合根用同一个 makeWorkerSeam 适配
using DigestWorker = std::function<std::string(const std::string &systemPrompt,
                                               const std::string &userPrompt)>;

// 私聊记录压缩器的 system prompt（参与者是"用户"和 Klein 自己；
// 其余要求与群版 kCompressSystem 一致）
extern const char *const kPrivateChatDigestSystem;

// 组装"【已压缩摘要】/【新增原文】"请求交给 worker，返回去首尾空白的新摘要；
// worker 为空、无返回或只返回空白时返回空串（调用方保留旧摘要，视为失败）
std::string mergeDigest(const DigestWorker &worker, const std::string &systemPrompt,
                        const std::string &oldDigest, const std::string &chunkLines);

#endif

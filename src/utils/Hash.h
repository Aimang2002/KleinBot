#ifndef UTILS_HASH_H
#define UTILS_HASH_H

/*
 * HMAC-SHA1 与十六进制编码：Webhook 签名（Network 模块）与观察通道
 * speaker 伪名化（Perception 模块）共用的最小哈希工具。
 * 实现自 WebhookSignature 提升而来，行为逐字节一致
 * （NetworkAuthTests 的已知向量兜底）。
 */
#include <string>
#include <string_view>

namespace utils
{
// HMAC-SHA1(key, message) → 40 位小写十六进制
std::string hmacSha1Hex(std::string_view key, std::string_view message);
}

#endif // UTILS_HASH_H

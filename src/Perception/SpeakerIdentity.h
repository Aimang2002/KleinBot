#ifndef PERCEPTION_SPEAKER_IDENTITY_H
#define PERCEPTION_SPEAKER_IDENTITY_H

/*
 * speaker 伪名化（T7b，用户定规）：观察存储中的 QQ 号一律存为
 * 加盐 HMAC-SHA1 单向哈希（40 位十六进制）。盐每安装一份，首次
 * 使用时生成、存 perception_meta 表。强度边界：QQ 号空间小（约10^10），
 * 拿到含盐完整库的攻击者可离线枚举——本哈希防的是消息表被单独拖走时
 * 直接读出"谁说了什么"与跨安装关联，不防定向攻击（文档明示）。
 * 群号不哈希：配置白名单已明文存群号，库内哈希无增益。
 */
#include <cstdint>
#include <string>

struct sqlite3;

namespace perception
{
// 读取或生成 speaker 盐（32 位十六进制）；perception_meta 表须已存在
std::string loadOrCreateSpeakerSalt(sqlite3 *db);

// 确定性伪名化：raw QQ → speaker_id（同盐同值，查表/join 直接可用）
std::string speakerIdOf(const std::string &salt, std::uint64_t qq);
}

#endif // PERCEPTION_SPEAKER_IDENTITY_H

#ifndef TOOL_CONTEXT_H
#define TOOL_CONTEXT_H

#include <cstdint>
#include <string>

// 工具调用期上下文：承载每次调用才确定的信息（与工具实例无关）
// 工具是全用户共享单例，user_id 每次对话都变，故走调用参数而非构造注入
struct ToolContext
{
    uint64_t user_id = 0;
    int64_t user_message_id = 0;
    std::string user_text;
    // 共享上下文作用域（ADR-019）：群聊时为群号，私聊/无为 0。
    // 图片类工具据此先查该作用域的短时资产库（24h），再回退 user_id 的个人永久库；
    // 生成结果在作用域非 0 时存进作用域库，不进个人库
    uint64_t shared_scope_id = 0;
};

// 一次对话所在的场合（工具按 Tool::offeredIn 声明自己适用哪里）。
// 群聊 = 有共享作用域（shared_scope_id 非 0），与 ADR-019 同一判据。
// 管理员私聊是私聊的一种，单列出来只为部署者专用的工具（认识账本，ADR-028）：
// 默认 offeredIn 恒真的工具在两种私聊里一样开放，只在群里开放的工具两种私聊里都没有
enum class ToolVenue
{
    Private,
    Group,
    ManagerPrivate,
};

// managerId 为 0（没配管理员）时没有管理员私聊
inline ToolVenue venueOf(uint64_t sharedScopeId, uint64_t userId, uint64_t managerId)
{
    if (sharedScopeId != 0)
        return ToolVenue::Group;
    return managerId != 0 && userId == managerId ? ToolVenue::ManagerPrivate : ToolVenue::Private;
}

#endif // TOOL_CONTEXT_H

#ifndef COMMAND_H
#define COMMAND_H

#include "../Port/InboundMessage.h"
#include "../Port/OutboundMessage.h"
#include <string>

struct CommandContext
{
    uint64_t user_id;
    uint64_t group_id;
    std::string message_type; // 群消息或私有消息
    const InboundMessage &data;
};

// 命令返回语义化消息，由活动协议适配器翻译为协议格式
// 不再有 MessageType 枚举——variant 的类型本身就承载了这层信息
struct CommandResult
{
    OutboundMessage payload;
};

class Command
{
public:
    virtual ~Command() = default;

    // 检查是否能处理该消息
    virtual bool canHandle(const std::string &message) = 0;
    // 执行命令，返回回复内容（空字符串表示不回复）
    virtual CommandResult execute(const CommandContext &ctx) = 0;
    // 检查是否需要管理员权限
    virtual bool requiresAdmin() const { return false; };
    // 命令帮助文本
    virtual std::string help() const = 0;

private:
};

#endif

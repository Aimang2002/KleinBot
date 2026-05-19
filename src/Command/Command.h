#ifndef COMMAND_H
#define COMMAND_H

#include "../JsonParse/JsonParse.h"
#include <string>

struct CommandContext
{
    uint64_t user_id;
    uint64_t group_id;
    std::string message_type; // 群消息或私有消息
    const JsonData &data;     // 存储原始消息数据
};

enum class MessageType
{
    Text,
    CQ
};

struct CommandResult
{
    std::string message;                  // 回复的内容
    MessageType type = MessageType::Text; // 响应类型
    bool handled = true;                  // 是否处理了这条消息
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

#ifndef INBOUND_MESSAGE_H
#define INBOUND_MESSAGE_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct InboundMessage
{
    std::uint64_t user_id = 0;
    std::string nickname;
    std::string card;

    std::uint64_t group_id = 0;
    std::string message_type;
    std::string post_type;

    std::string raw_message;
    std::string plain_text;
    std::string message_data_url;

    // 入站 @ 的结构化记录：at 段的 qq 列表（"all" 广播不含 bot 个人，不记录）。
    // 替代旧 raw_message 的 CQ:at 字符串匹配——群聊触发门槛与指向性回复都以它为准
    std::vector<std::uint64_t> mentioned_ids;

    std::int64_t message_id = 0;
    // 个别实现端（如 Lagrange 部分 API）消息 ID 为字符串：原样保存供 reply 段回填
    std::string message_id_raw;
    std::int64_t message_timestamp = 0;
    std::size_t payload_size_bytes = 0;

    // notice/request 事件字段（message 事件不填充），由 EventRouter 按 key 路由
    std::string notice_type;        // notice：group_increase / group_decrease / notify / ...
    std::string sub_type;           // notify 家族细分：poke；group_increase: approve / invite
    std::uint64_t target_id = 0;    // poke：被戳者
    std::uint64_t operator_id = 0;  // 操作者 / 邀请人 / 踢人管理员
    std::string request_type;       // request：friend / group
    std::string comment;            // 好友申请验证语
    std::string flag;               // 请求处理令牌（后续 set_friend_add_request 用）
};

#endif // INBOUND_MESSAGE_H

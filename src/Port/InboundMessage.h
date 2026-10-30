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
    // 零成本表情信号（R1.1）：image 段 data.sub_type==1 或 data.summary 非空视为表情包
    // 形态发送；summary 原样保存（如 "[动画表情]"）。与 message_data_url 同取最后一个 image 段
    bool image_is_sticker = false;
    std::string image_summary;
    // mface 段的 data.summary（如 "[狗头]"）与 face 段的 "face:<id>"，按出现顺序。
    // 只有群观察路径消费（拼成 [表情 …] 进群缓冲），私聊路径不读
    std::vector<std::string> face_summaries;
    // 没有正文也没有可下载 url 的段的文字标签（2026-10-30，真机：这类消息此前整条退化成
    // 裸 "[图片]"）：video→"[视频]"、record→"[语音]"、file→"[文件 名]"、json→"[卡片 提示语]"、
    // forward→"[合并转发]"、markdown/keyboard→"[卡片]"。按出现顺序；群观察路径与 @ 路径都消费
    std::vector<std::string> attachment_labels;

    // 入站 @ 的结构化记录：at 段的 qq 列表（"all" 广播不含 bot 个人，不记录）。
    // 替代旧 raw_message 的 CQ:at 字符串匹配——群聊触发门槛与指向性回复都以它为准
    std::vector<std::uint64_t> mentioned_ids;

    std::int64_t message_id = 0;
    // 个别实现端（如 Lagrange 部分 API）消息 ID 为字符串：原样保存供 reply 段回填
    std::string message_id_raw;
    // 入站引用回复：被引用消息的 ID（双形态，同 message_id）。观察通道用它在
    // 群消息流中还原回复链——同线程消息互相加权（T7b）
    std::int64_t reply_to_message_id = 0;
    std::string reply_to_message_id_raw;
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

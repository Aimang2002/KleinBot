#ifndef PUSH_DELIVERY_H
#define PUSH_DELIVERY_H

#include "PushContent.h"
#include "PushSettings.h"

#include "../Port/MessageSenderPort.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

// 推送门统一接缝：Target + 类型化内容块 → 校验（白名单/频率/大小）→
// 人格润色 → 现有出站管线。契约状态为草案（AGENTS.md 窄腰模型一节，
// 转正条件：≥2 真实消费者 + 一次未破坏契约的扩展）。
//
// 本类是全部边界规则的唯一执行点，与入口形态无关：
//   - HTTP 推送门（PushServer）解析 POST 后调用 deliver()
//   - 阶段 2 的 MCP 门（poll_due_tasks 约定 + tools/call 的内容翻译）
//     将把翻译结果送入同一个 deliver()，两门共享此接缝与边界规则，
//     届时不在本类之外另写一套校验/润色/投递
//
// 内容类型当前支持 text / image；
// TODO(阶段2): voice 内容块 → base64 落临时文件 → VoiceMessage（复用语音附件清理）
class PushDelivery
{
public:
    // 润色器：输入外部原文，输出口吻化文本；失败返回空串（接缝兜底直发原文）。
    // 注入而非直连 ChatService，测试与将来的 MCP 门共用同一接缝
    using Renderer = std::function<std::string(const std::string &)>;

    explicit PushDelivery(PushSettings settings, MessageSenderPort &sender, Renderer renderer);

    // 单次投递：全部内容块按序送达同一目标。任何一项校验失败则整次拒绝，
    // 不产生部分投递。线程安全（HTTP 处理线程并发调用）
    PushReject deliver(const PushTarget &target, const std::vector<PushContent> &contents,
                       bool skipRender);

private:
    bool targetAllowed(const PushTarget &target) const;
    bool rateAllow(const std::string &targetKey);
    PushReject checkContents(const std::vector<PushContent> &contents) const;
    std::string render(const std::string &raw) const;

    PushSettings settings;
    MessageSenderPort &sender;
    Renderer renderer;

    // 每目标滑动小时窗频率计数（键形如 "g:123" / "u:456"）
    std::mutex rateMutex;
    std::map<std::string, std::deque<std::chrono::steady_clock::time_point>> rateWindows;
};

#endif // PUSH_DELIVERY_H

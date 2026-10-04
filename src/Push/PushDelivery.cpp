#include "PushDelivery.h"
#include "PushContent.h"

#include "../Port/OutboundDelivery.h"
#include "../Port/OutboundMessage.h"
#include "../Log/Log.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace
{
// 内容大小上限：text 与出站长文本同量级；image base64 上限约对应 4.5MB 二进制
constexpr std::size_t kMaxTextBytes = 4096;
constexpr std::size_t kMaxImageBase64Bytes = 6ULL * 1024ULL * 1024ULL;
constexpr auto kRateWindow = std::chrono::hours(1);

std::string targetKey(const PushTarget &target)
{
    return (target.isGroup ? "g:" : "u:") + std::to_string(target.id);
}

std::string localTimeText()
{
    const std::time_t now = std::time(nullptr);
    std::tm local{};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return buffer;
}

bool containsId(const std::vector<std::int64_t> &list, std::int64_t id)
{
    return std::find(list.begin(), list.end(), id) != list.end();
}
}

PushDelivery::PushDelivery(PushSettings settings, MessageSenderPort &sender, Renderer renderer)
    : settings(std::move(settings)), sender(sender), renderer(std::move(renderer))
{
}

bool PushDelivery::targetAllowed(const PushTarget &target) const
{
    if (target.id <= 0)
        return false;
    return target.isGroup ? containsId(settings.allowedGroups, target.id)
                          : containsId(settings.allowedUsers, target.id);
}

bool PushDelivery::rateAllow(const std::string &key)
{
    const auto now = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(rateMutex);
    auto &stamps = rateWindows[key];
    while (!stamps.empty() && now - stamps.front() >= kRateWindow)
        stamps.pop_front();
    if (stamps.size() >= static_cast<std::size_t>(settings.maxPerHour))
        return false;
    stamps.push_back(now);
    return true;
}

PushReject PushDelivery::checkContents(const std::vector<PushContent> &contents) const
{
    if (contents.empty())
        return PushReject::InvalidContent;
    for (const PushContent &content : contents)
    {
        if (content.data.empty())
            return PushReject::InvalidContent;
        if (content.type == "text")
        {
            if (content.data.size() > kMaxTextBytes)
                return PushReject::TextTooLarge;
        }
        else if (content.type == "image")
        {
            if (content.data.size() > kMaxImageBase64Bytes)
                return PushReject::ImageTooLarge;
        }
        else
            return PushReject::UnsupportedType;
    }
    return PushReject::Ok;
}

std::string PushDelivery::render(const std::string &raw) const
{
    // 渲染失败（含模型错误返回空串）时兜底原文直发，与提醒线程同语义：
    // 宁可消息朴素，不可消息丢失
    if (!renderer)
        return raw;
    const std::string prompt = "现在是" + localTimeText() +
                               "。外部系统给你递来一段要转达的内容：\n" + raw +
                               "\n请以你自己的口吻把这条消息说出来，信息保持完整，"
                               "简短自然，不要添加引号或任何前缀解释。";
    const std::string rendered = renderer(prompt);
    return rendered.empty() ? raw : rendered;
}

PushReject PushDelivery::deliver(const PushTarget &target, const std::vector<PushContent> &contents,
                                 bool skipRender)
{
    const std::string key = targetKey(target);
    if (!targetAllowed(target))
    {
        LOG_WARNING("推送门拒绝：目标不在白名单（" + key + "）");
        return PushReject::DeniedTarget;
    }
    const PushReject contentCheck = checkContents(contents);
    if (contentCheck != PushReject::Ok)
    {
        LOG_WARNING("推送门拒绝：内容校验未通过（" + key + "，原因码 " +
                    std::to_string(static_cast<int>(contentCheck)) + "）");
        return contentCheck;
    }
    if (!rateAllow(key))
    {
        LOG_WARNING("推送门拒绝：超出该目标每小时频率上限（" + key + "，上限 " +
                    std::to_string(settings.maxPerHour) + "）");
        return PushReject::RateLimited;
    }

    const OutboundTarget outboundTarget =
        target.isGroup ? OutboundTarget(GroupMessageTarget{std::to_string(target.id)})
                       : OutboundTarget(DirectMessageTarget{std::to_string(target.id)});
    for (const PushContent &content : contents)
    {
        if (content.type == "image")
        {
            sender.deliver(OutboundDelivery{
                outboundTarget, ImageMessage{ImageMessage::Source::Base64, content.data}});
        }
        else
        {
            const std::string text = skipRender ? content.data : render(content.data);
            sender.deliver(OutboundDelivery{outboundTarget, TextMessage{text}});
        }
    }
    LOG_INFO("推送门已受理：" + key + "，内容块 " + std::to_string(contents.size()) +
             " 个" + (skipRender ? "（跳过润色）" : ""));
    return PushReject::Ok;
}

#include "GroupPresence.h"

#include <algorithm>
#include <set>

namespace affect
{
namespace
{
// 账本条数硬上限（资源护栏）：窗口按时间修剪，这里只防极端刷屏把内存撑大
constexpr std::size_t kMaxEntries = 4096;
} // namespace

void GroupPresence::append(Entry entry)
{
    // 写入时修剪：时间戳大致有序（协议端时间与本机时钟可能有几秒偏差），
    // 从队头丢掉已滑出窗口的；查询仍逐条按窗口过滤，不依赖严格有序
    const std::int64_t cutoff = entry.at - rules.shareWindowSeconds;
    while (!recent.empty() && recent.front().at < cutoff)
        recent.pop_front();
    recent.push_back(entry);
    while (recent.size() > kMaxEntries)
        recent.pop_front();
}

void GroupPresence::noteOthersMessage(std::int64_t at, std::uint64_t speakerId)
{
    append({at, Who::Other, speakerId});
}

void GroupPresence::noteSpeech(std::int64_t at, SpeechKind kind)
{
    append({at, kind == SpeechKind::Answer ? Who::Answer : Who::Unprompted, 0});
}

void GroupPresence::withdraw(std::int64_t at)
{
    withdrawnUntil = std::max(withdrawnUntil, at + rules.withdrawSeconds);
}

bool GroupPresence::withdrawn(std::int64_t now) const
{
    return now < withdrawnUntil;
}

PresenceView GroupPresence::view(std::int64_t now) const
{
    PresenceView result;
    std::set<std::uint64_t> speakers;
    const std::int64_t cutoff = now - rules.shareWindowSeconds;
    for (const Entry &entry : recent)
    {
        if (entry.at < cutoff)
            continue;
        switch (entry.who)
        {
        case Who::Other:
            result.othersMessages += 1;
            speakers.insert(entry.speakerId);
            break;
        case Who::Answer:
            result.answers += 1;
            break;
        case Who::Unprompted:
            result.unprompted += 1;
            break;
        }
    }
    result.otherSpeakers = static_cast<int>(speakers.size());
    result.withdrawnUntil = withdrawnUntil;
    result.withdrawn = withdrawn(now);
    // 份额：再说一句之后，她的主动条数仍不超过其他人平均每人的条数
    const bool shareLeft =
        result.otherSpeakers > 0 &&
        static_cast<long long>(result.unprompted + 1) * result.otherSpeakers <=
            static_cast<long long>(result.othersMessages);
    result.mayVolunteer = !result.withdrawn && shareLeft;
    return result;
}

bool GroupPresence::mayVolunteer(std::int64_t now) const
{
    return view(now).mayVolunteer;
}
} // namespace affect

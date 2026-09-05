#include "CapabilityBroker.h"
#include "../Log/Log.h"

#include <algorithm>
#include <cctype>

namespace
{
std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool nameContains(const std::string &appName, const std::string &needle)
{
    return toLower(appName).find(toLower(needle)) != std::string::npos;
}
}

bool featureSupported(const std::string &appName, BotFeature feature)
{
    // 静态能力表（roadmap §2 协议依赖矩阵）。实现端名按 get_version_info 的
    // app_name 宽松匹配；未列出的实现端全部 false（D12 保守默认）
    switch (feature)
    {
    case BotFeature::InputStatus:
        // set_input_status：仅 NapCat 确认支持。LLOneBot 存疑按不支持处理，
        // 试调用探测留待 T5 落地后按真实语义补（HttpApiChannel 会把 404 归为
        // networkError，跨传输不可靠，当前不做不可靠探测）
        return nameContains(appName, "napcat");
    case BotFeature::GroupPoke:
        // NapCat/LLOneBot 支持；Lagrange 发送动作名不同，本版本不适配
        return nameContains(appName, "napcat") || nameContains(appName, "llonebot");
    }
    return false;
}

std::optional<std::string> appNameFromInfo(const OneBotApiResult &result)
{
    if (result.networkError || result.retcode != 0 || !result.data.is_object())
    {
        return std::nullopt;
    }
    const std::string appName = result.data.value("app_name", "");
    if (appName.empty())
    {
        return std::nullopt;
    }
    return appName;
}

CapabilityBroker::CapabilityBroker(OneBotApiChannel &api)
    : probe_([&api] {
        return api.call("get_version_info", nlohmann::json::object(),
                        std::chrono::seconds(5));
    })
{
}

CapabilityBroker::CapabilityBroker(VersionProbe probe) : probe_(std::move(probe))
{
}

void CapabilityBroker::poll(std::int64_t nowSeconds)
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (state_ == State::Ready || nowSeconds < retryAt_)
        {
            return;
        }
        // 占位进入 Failed：探测期间 supports() 保守返回 false，
        // 失败冷却从尝试时刻起算（poll 只会由 pollingThread 串行调用，无重入）
        state_ = State::Failed;
        retryAt_ = nowSeconds + 60;
    }
    runProbe();
}

bool CapabilityBroker::supports(BotFeature feature) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != State::Ready)
    {
        return false;
    }
    switch (feature)
    {
    case BotFeature::InputStatus:
        return inputStatus_;
    case BotFeature::GroupPoke:
        return groupPoke_;
    }
    return false;
}

std::string CapabilityBroker::endpointSummary() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return summary_;
}

void CapabilityBroker::runProbe()
{
    const std::optional<OneBotApiResult> result = probe_();

    std::lock_guard<std::mutex> lock(mutex_);
    const std::optional<std::string> appName =
        result ? appNameFromInfo(*result) : std::nullopt;
    if (!appName)
    {
        // 维持 Failed 与已设冷却，下一轮重试；期间所有能力位保持 false
        LOG_WARNING("能力探测失败，60 秒后重试（未就绪能力对应功能保持关闭）");
        return;
    }
    inputStatus_ = featureSupported(*appName, BotFeature::InputStatus);
    groupPoke_ = featureSupported(*appName, BotFeature::GroupPoke);
    summary_ = *appName;
    state_ = State::Ready;
    LOG_INFO("能力协商完成：" + *appName + "（InputStatus=" +
             (inputStatus_ ? "开" : "关") + "，GroupPoke=" + (groupPoke_ ? "开" : "关") +
             "）");
}

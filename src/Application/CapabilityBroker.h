#ifndef CAPABILITY_BROKER_H
#define CAPABILITY_BROKER_H

#include "../Network/OneBotApiChannel.h"

#include <functional>
#include <mutex>
#include <optional>
#include <string>

// 能力词汇表（D12）：业务代码只认识这些枚举值，永远不认识实现端名字。
// 新能力准入按 D12 三标准评审（抽象得了/有真实功能想要/降级得体面），
// 实现端支持与否只存在于静态能力表的数据里
enum class BotFeature
{
    InputStatus, // set_input_status：私聊"对方正在输入"提示
    GroupPoke    // group_poke：主动戳一戳
};

// 静态能力表：实现端差异的唯一知识存放点（纯函数，直接单元测试）。
// 未列出的实现端一律 false——保守默认，功能静默关闭，主路径无感
bool featureSupported(const std::string &appName, BotFeature feature);

// 从 get_version_info 响应提取实现端名（协议解读与表查询分离，各自可测）
std::optional<std::string> appNameFromInfo(const OneBotApiResult &result);

// 能力协商（T2）：启动后探测一次实现端身份，缓存能力位供业务查询。
// 一套代码设计（D12）：本类与消费者只认识 BotFeature，实现端名字到不了业务层。
// 探测在 pollingThread 内同步执行（≤5s，启动期一次性，可接受）；成功后进程内
// 缓存永不重探，失败按 60s 冷却重试直至成功。未就绪/不支持一律返回 false
class CapabilityBroker
{
public:
    explicit CapabilityBroker(OneBotApiChannel &api);

    // 探测源可注入：生产构造绑 ApiChannel，测试注入 fake
    using VersionProbe = std::function<std::optional<OneBotApiResult>()>;
    explicit CapabilityBroker(VersionProbe probe);

    // pollingThread 周期调用；就绪后为无锁快速路径之外的一次互斥读，开销可忽略
    void poll(std::int64_t nowSeconds);

    bool supports(BotFeature feature) const;

    // 实现端名（如 "NapCat"）；未探测成功返回空串，供日志观测
    std::string endpointSummary() const;

private:
    enum class State
    {
        Waiting, // 尚未探测
        Ready,   // 已探测并缓存（终态：能力集不随后续变化）
        Failed   // 上次失败，冷却中
    };

    void runProbe();

    VersionProbe probe_;
    mutable std::mutex mutex_;
    State state_ = State::Waiting;
    std::int64_t retryAt_ = 0; // Failed 冷却截止（秒）；探测期间置为 now+60
    bool inputStatus_ = false;
    bool groupPoke_ = false;
    std::string summary_;
};

#endif // CAPABILITY_BROKER_H

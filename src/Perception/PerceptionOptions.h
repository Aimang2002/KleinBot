#ifndef PERCEPTION_OPTIONS_H
#define PERCEPTION_OPTIONS_H

/*
 * 观察通道（v2.4.1 T7）运行参数：只看不说、0 LLM、默认关。
 * 白名单起步是 roadmap 的保守答案——observeGroups 为空时即使
 * enabled 也不观察任何群。
 */
#include <cstdint>
#include <vector>

struct PerceptionOptions
{
    bool enabled = false;
    std::vector<std::uint64_t> observeGroups;

    bool observing() const { return enabled && !observeGroups.empty(); }
};

#endif // PERCEPTION_OPTIONS_H

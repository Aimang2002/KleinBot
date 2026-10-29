#ifndef GLANCE_OPTIONS_H
#define GLANCE_OPTIONS_H

/*
 * 群聊瞥视（R1.1）的模块 Options：由 Bootstrap/RuntimeSettings 从配置节
 * glance 映射而来（Perception 不 include Configuration）。
 * 预算是感官边界（资源护栏），不是行为规则：超预算只是"没看"，不触发任何回应。
 */
struct GlanceOptions
{
    bool enabled = false;       // 总开关；默认关闭（零行为变化）
    int perGroupPerMinute = 6;  // 每群滚动 60 秒窗口内的视觉调用上限（1–60）
};

#endif // GLANCE_OPTIONS_H

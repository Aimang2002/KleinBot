#ifndef EVENT_ROUTER_H
#define EVENT_ROUTER_H

#include "../Port/InboundMessage.h"

#include <string>
#include <unordered_map>
#include <vector>

// 事件处理器：由组合根构造并注册，worker 线程内同步执行；
// 抛异常由 KeyedTaskScheduler 的统一 errorHandler 兜底，不会炸掉 worker
class EventHandler
{
public:
    virtual ~EventHandler() = default;
    virtual void handle(const InboundMessage &event) = 0;
};

// notice/request 事件路由：按 routeKey(event) 分发给已注册 handler。
// 无订阅者的事件静默丢弃——与 v2.4.0 对 notice/request 的静默忽略行为一致，
// 未知事件类型因此不需要跟随版本升级处理器
class EventRouter
{
public:
    // 同一 key 可挂多个 handler，按注册顺序调用；handler 生命周期由组合根保证
    void subscribe(const std::string &key, EventHandler &handler);
    void dispatch(const InboundMessage &event) const;

    // 路由键：post_type + "." + (notice_type|request_type)；
    // notify 家族（戳一戳等）再细分到 sub_type
    static std::string routeKey(const InboundMessage &event);

private:
    std::unordered_map<std::string, std::vector<EventHandler *>> handlers;
};

#endif // EVENT_ROUTER_H

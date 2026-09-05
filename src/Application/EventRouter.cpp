#include "EventRouter.h"
#include "../Log/Log.h"

void EventRouter::subscribe(const std::string &key, EventHandler &handler)
{
    handlers[key].push_back(&handler);
}

void EventRouter::dispatch(const InboundMessage &event) const
{
    const auto node = handlers.find(routeKey(event));
    if (node == handlers.end())
    {
        LOG_DEBUG("事件无订阅者，已忽略：" + routeKey(event));
        return;
    }
    for (EventHandler *handler : node->second)
    {
        handler->handle(event);
    }
}

std::string EventRouter::routeKey(const InboundMessage &event)
{
    if (event.post_type == "notice")
    {
        std::string key = "notice." + event.notice_type;
        if (event.notice_type == "notify")
        {
            key += "." + event.sub_type;
        }
        return key;
    }
    if (event.post_type == "request")
    {
        return "request." + event.request_type;
    }
    return event.post_type;
}

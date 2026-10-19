#include "TransportConfig.h"

std::string transportModeName(TransportMode mode)
{
    switch (mode)
    {
    case TransportMode::ForwardWebSocket:
        return "forward_websocket";
    case TransportMode::ReverseWebSocket:
        return "reverse_websocket";
    case TransportMode::Http:
        return "http";
    }
    return "unknown";
}

std::string transportModeDisplayName(TransportMode mode)
{
    switch (mode)
    {
    case TransportMode::ForwardWebSocket:
        return "服务器模式";
    case TransportMode::ReverseWebSocket:
        return "客户端模式";
    case TransportMode::Http:
        return "HTTP";
    }
    return "unknown";
}

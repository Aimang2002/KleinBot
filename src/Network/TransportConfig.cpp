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

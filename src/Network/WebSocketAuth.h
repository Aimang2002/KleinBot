#ifndef WEBSOCKET_AUTH_H
#define WEBSOCKET_AUTH_H

#include <string>
#include <string_view>

namespace WebSocketAuth
{
std::string buildAuthorizationValue(const std::string &token);
bool isAuthorized(std::string_view authorizationValue, std::string_view expectedToken);
}

#endif // WEBSOCKET_AUTH_H

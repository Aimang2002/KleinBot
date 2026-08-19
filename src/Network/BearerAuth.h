#ifndef BEARER_AUTH_H
#define BEARER_AUTH_H

#include <string>
#include <string_view>

namespace BearerAuth
{
std::string buildAuthorizationValue(const std::string &token);
bool isAuthorized(std::string_view authorizationValue, std::string_view expectedToken);
}

#endif // BEARER_AUTH_H

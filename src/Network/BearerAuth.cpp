#include "BearerAuth.h"

#include <cctype>
#include <cstddef>

namespace
{
bool equalsIgnoreCase(std::string_view left, std::string_view right)
{
    if (left.size() != right.size())
    {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index)
    {
        const auto leftCharacter = static_cast<unsigned char>(left[index]);
        const auto rightCharacter = static_cast<unsigned char>(right[index]);
        if (std::tolower(leftCharacter) != std::tolower(rightCharacter))
        {
            return false;
        }
    }
    return true;
}

bool constantTimeEqual(std::string_view presentedToken, std::string_view expectedToken)
{
    std::size_t difference = presentedToken.size() ^ expectedToken.size();
    for (std::size_t index = 0; index < expectedToken.size(); ++index)
    {
        const unsigned char presentedCharacter = index < presentedToken.size()
            ? static_cast<unsigned char>(presentedToken[index])
            : 0;
        difference |= presentedCharacter ^ static_cast<unsigned char>(expectedToken[index]);
    }
    return difference == 0;
}
}

std::string BearerAuth::buildAuthorizationValue(const std::string &token)
{
    return "Bearer " + token;
}

bool BearerAuth::isAuthorized(std::string_view authorizationValue, std::string_view expectedToken)
{
    if (expectedToken.empty())
    {
        return true;
    }

    const std::size_t separator = authorizationValue.find(' ');
    if (separator == std::string_view::npos ||
        !equalsIgnoreCase(authorizationValue.substr(0, separator), "Bearer"))
    {
        return false;
    }

    std::size_t tokenStart = separator;
    while (tokenStart < authorizationValue.size() && authorizationValue[tokenStart] == ' ')
    {
        ++tokenStart;
    }

    return constantTimeEqual(authorizationValue.substr(tokenStart), expectedToken);
}

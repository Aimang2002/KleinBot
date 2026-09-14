#include "WebhookSignature.h"
#include "../utils/Hash.h"

#include <string_view>

namespace
{
bool constantTimeEqual(std::string_view presented, std::string_view expected)
{
    std::size_t difference = presented.size() ^ expected.size();
    for (std::size_t index = 0; index < expected.size(); ++index)
    {
        const unsigned char presentedCharacter = index < presented.size()
            ? static_cast<unsigned char>(presented[index])
            : 0;
        difference |= presentedCharacter ^ static_cast<unsigned char>(expected[index]);
    }
    return difference == 0;
}
}

namespace WebhookSignature
{

std::string signSha1(std::string_view body, std::string_view secret)
{
    // SHA1/HMAC 实现已提升为 utils/Hash（观察通道 speaker 伪名化共用）
    return "sha1=" + utils::hmacSha1Hex(secret, body);
}

bool isAuthorized(std::string_view signatureValue, std::string_view body,
                  std::string_view expectedSecret)
{
    if (expectedSecret.empty())
        return true;
    return constantTimeEqual(signatureValue, signSha1(body, expectedSecret));
}

}

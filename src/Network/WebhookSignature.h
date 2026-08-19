#ifndef WEBHOOK_SIGNATURE_H
#define WEBHOOK_SIGNATURE_H

#include <string>
#include <string_view>

namespace WebhookSignature
{
std::string signSha1(std::string_view body, std::string_view secret);
bool isAuthorized(std::string_view signatureValue, std::string_view body,
                  std::string_view expectedSecret);
}

#endif

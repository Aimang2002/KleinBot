#include "WebhookSignature.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <vector>

namespace
{
std::uint32_t rotateLeft(std::uint32_t value, unsigned bits)
{
    return (value << bits) | (value >> (32U - bits));
}

std::array<std::uint8_t, 20> sha1(std::string_view input)
{
    std::vector<std::uint8_t> message(input.begin(), input.end());
    const std::uint64_t bitLength = static_cast<std::uint64_t>(message.size()) * 8U;
    message.push_back(0x80U);
    while ((message.size() % 64U) != 56U)
        message.push_back(0U);
    for (int shift = 56; shift >= 0; shift -= 8)
        message.push_back(static_cast<std::uint8_t>((bitLength >> shift) & 0xffU));

    std::uint32_t h0 = 0x67452301U;
    std::uint32_t h1 = 0xefcdab89U;
    std::uint32_t h2 = 0x98badcfeU;
    std::uint32_t h3 = 0x10325476U;
    std::uint32_t h4 = 0xc3d2e1f0U;

    for (std::size_t offset = 0; offset < message.size(); offset += 64U)
    {
        std::array<std::uint32_t, 80> words{};
        for (std::size_t index = 0; index < 16U; ++index)
        {
            const std::size_t position = offset + index * 4U;
            words[index] = (static_cast<std::uint32_t>(message[position]) << 24U) |
                           (static_cast<std::uint32_t>(message[position + 1U]) << 16U) |
                           (static_cast<std::uint32_t>(message[position + 2U]) << 8U) |
                           static_cast<std::uint32_t>(message[position + 3U]);
        }
        for (std::size_t index = 16U; index < words.size(); ++index)
        {
            words[index] = rotateLeft(
                words[index - 3U] ^ words[index - 8U] ^
                    words[index - 14U] ^ words[index - 16U],
                1U);
        }

        std::uint32_t a = h0;
        std::uint32_t b = h1;
        std::uint32_t c = h2;
        std::uint32_t d = h3;
        std::uint32_t e = h4;

        for (std::size_t index = 0; index < words.size(); ++index)
        {
            std::uint32_t function = 0;
            std::uint32_t constant = 0;
            if (index < 20U)
            {
                function = (b & c) | ((~b) & d);
                constant = 0x5a827999U;
            }
            else if (index < 40U)
            {
                function = b ^ c ^ d;
                constant = 0x6ed9eba1U;
            }
            else if (index < 60U)
            {
                function = (b & c) | (b & d) | (c & d);
                constant = 0x8f1bbcdcU;
            }
            else
            {
                function = b ^ c ^ d;
                constant = 0xca62c1d6U;
            }

            const std::uint32_t temporary =
                rotateLeft(a, 5U) + function + e + constant + words[index];
            e = d;
            d = c;
            c = rotateLeft(b, 30U);
            b = a;
            a = temporary;
        }

        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<std::uint8_t, 20> digest{};
    const std::array<std::uint32_t, 5> words{h0, h1, h2, h3, h4};
    for (std::size_t index = 0; index < words.size(); ++index)
    {
        digest[index * 4U] = static_cast<std::uint8_t>((words[index] >> 24U) & 0xffU);
        digest[index * 4U + 1U] = static_cast<std::uint8_t>((words[index] >> 16U) & 0xffU);
        digest[index * 4U + 2U] = static_cast<std::uint8_t>((words[index] >> 8U) & 0xffU);
        digest[index * 4U + 3U] = static_cast<std::uint8_t>(words[index] & 0xffU);
    }
    return digest;
}

std::array<std::uint8_t, 20> hmacSha1(std::string_view body, std::string_view secret)
{
    constexpr std::size_t blockSize = 64U;
    std::array<std::uint8_t, blockSize> key{};
    if (secret.size() > blockSize)
    {
        const auto digest = sha1(secret);
        std::copy(digest.begin(), digest.end(), key.begin());
    }
    else
    {
        std::copy(secret.begin(), secret.end(), key.begin());
    }

    std::string inner(blockSize, '\0');
    std::string outer(blockSize, '\0');
    for (std::size_t index = 0; index < blockSize; ++index)
    {
        inner[index] = static_cast<char>(key[index] ^ 0x36U);
        outer[index] = static_cast<char>(key[index] ^ 0x5cU);
    }
    inner.append(body.data(), body.size());
    const auto innerDigest = sha1(inner);
    outer.append(reinterpret_cast<const char *>(innerDigest.data()), innerDigest.size());
    return sha1(outer);
}

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

std::string WebhookSignature::signSha1(std::string_view body, std::string_view secret)
{
    const auto digest = hmacSha1(body, secret);
    std::ostringstream output;
    output << "sha1=" << std::hex << std::setfill('0');
    for (const std::uint8_t byte : digest)
        output << std::setw(2) << static_cast<unsigned>(byte);
    return output.str();
}

bool WebhookSignature::isAuthorized(std::string_view signatureValue,
                                    std::string_view body,
                                    std::string_view expectedSecret)
{
    if (expectedSecret.empty())
        return true;
    return constantTimeEqual(signatureValue, signSha1(body, expectedSecret));
}

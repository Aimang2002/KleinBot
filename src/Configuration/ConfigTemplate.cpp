#include "ConfigTemplate.h"
#include "ConfigDiagnostic.h"
#include "ConfigWriter.h"
#include "SchemaConfig.h"

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <random>
#include <vector>

namespace
{
std::string toHex(std::uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (int index = 15; index >= 0; --index)
    {
        result[static_cast<std::size_t>(index)] = digits[value & 0xF];
        value >>= 4;
    }
    return result;
}
}

namespace ConfigTemplate
{

std::string generateWebUiToken()
{
    static std::mutex mutex;
    static std::mt19937_64 engine(std::random_device{}());
    static std::uniform_int_distribution<std::uint64_t> value;
    std::lock_guard<std::mutex> lock(mutex);
    return toHex(value(engine)) + toHex(value(engine));
}

nlohmann::json defaultDocument(const std::string &webUiToken)
{
    nlohmann::json document = nlohmann::json::parse(R"({
        "schema_version": 1,
        "bot": {"id": 123456789, "manager_id": 0, "name": "Klein", "group_chat_enabled": true},
        "chat": {"default_model": "your-chat-model"},
        "models": {},
        "communication": {
            "protocol": {"type": "onebot", "options": {}},
            "active_transport": "onebot-reverse",
            "transports": {
                "onebot-reverse": {
                    "type": "reverse_websocket",
                    "bind": "127.0.0.1",
                    "port": 8600,
                    "path": "/onebot",
                    "access_token": {"literal": ""}
                }
            }
        },
        "webui": {"enabled": true, "bind": "127.0.0.1"}
    })");
    document["webui"]["port"] = kDefaultWebUiPort;
    document["webui"]["access_token"] = {{"literal", webUiToken}};
    return document;
}

bool createIfMissing(const std::string &path, std::string &createdToken)
{
    std::error_code error;
    if (std::filesystem::exists(path, error))
        return false;

    createdToken = generateWebUiToken();
    std::vector<ConfigDiagnostic> diagnostics;
    if (!writeJsonAtomically(path, defaultDocument(createdToken), diagnostics))
    {
        createdToken.clear();
        return false;
    }
    return true;
}

}

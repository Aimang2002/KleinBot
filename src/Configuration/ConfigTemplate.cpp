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
    // 骨架 = 全部配置节的完整形态（结构对齐 config.example.json），值为安全占位：
    // 面板首跑即能看到并补全所有模块，而不是只看到最小集合；密钥一律空串（不用
    // from_env 引用不存在的环境变量，避免首跑日志出现成片告警）。观察通道不进骨架
    // （用户定规 2026-09-15：开关与监控群集是运行时状态，由数据库独占管理）
    nlohmann::json document = nlohmann::json::parse(R"({
        "schema_version": 1,
        "bot": {"id": 123456789, "manager_id": 0, "name": "Klein", "group_chat_enabled": true},
        "chat": {
            "default_model": "your-chat-model",
            "temperature": 1.0,
            "top_p": 1.0,
            "frequency_penalty": 0.0,
            "presence_penalty": 0.0,
            "max_message_tokens": 4096,
            "message_survival_seconds": 3600
        },
        "models": {
            "drawing": {"model": "", "endpoint": "", "api_key": "", "api_standard": "OpenAI"},
            "vision": {"model": "", "endpoint": "", "api_key": "", "api_standard": "OpenAI"},
            "worker": {"model": "", "endpoint": "", "api_key": "", "api_standard": "OpenAI"}
        },
        "voice": {
            "enabled": false,
            "host": "http://127.0.0.1",
            "port": "9880",
            "reference_audio": "",
            "reference_text": ""
        },
        "memory": {
            "enabled": true,
            "model": "your-chat-model",
            "batch_turns": 10,
            "idle_minutes": 30,
            "recall_limit": 8
        },
        "web_search": {
            "enabled": false,
            "provider": "tavily",
            "endpoint": "https://api.tavily.com/search",
            "api_key": "",
            "search_depth": "basic",
            "max_results": 5,
            "max_content_chars": 2000,
            "max_response_bytes": 2097152,
            "connect_timeout_ms": 5000,
            "request_timeout_ms": 15000
        },
        "web_fetch": {
            "enabled": false,
            "max_content_chars": 12000,
            "max_response_bytes": 2097152,
            "connect_timeout_ms": 5000,
            "request_timeout_ms": 20000,
            "cache_ttl_seconds": 900,
            "cache_max_entries": 32
        },
        "storage": {
            "conversation_database": "source/.conversations.db",
            "image_assets": "source/image_assets"
        },
        "network": {"proxy": ""},
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
                },
                "onebot-http": {
                    "type": "http",
                    "api": {
                        "base_url": "http://127.0.0.1:3000",
                        "access_token": ""
                    },
                    "events": {
                        "bind": "127.0.0.1",
                        "port": 8080,
                        "path": "/onebot/events",
                        "secret": ""
                    }
                }
            },
            "defaults": {
                "connect_timeout_ms": 5000,
                "request_timeout_ms": 15000,
                "max_event_body_bytes": 1048576
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

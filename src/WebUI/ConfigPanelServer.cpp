#include "ConfigPanelServer.h"
#include "../Configuration/ConfigWriter.h"
#include "../Log/Log.h"
#include "../Network/BearerAuth.h"
#include "../../Library/httplib/httplib.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <memory>
#include <set>
#include <sstream>
#include <thread>

namespace
{
using json = nlohmann::json;

// 面板页面路径：相对进程工作目录，与 HelpCommand 读 help.txt 同一约定
const char *const kPanelPagePath = "panel.html";

json diagnosticsToArray(const std::vector<ConfigDiagnostic> &diagnostics)
{
    json array = json::array();
    for (const ConfigDiagnostic &diagnostic : diagnostics)
    {
        array.push_back({{"severity", configSeverityName(diagnostic.severity)},
                         {"path", diagnostic.path},
                         {"message", diagnostic.message}});
    }
    return array;
}

json diffToArray(const ConfigDiff &diff)
{
    json array = json::array();
    for (const ConfigChange &change : diff.changes())
        array.push_back({{"path", change.path},
                         {"impact", configChangeImpactName(change.impact)}});
    return array;
}

bool readJsonFile(const std::string &path, json &document,
                  std::vector<ConfigDiagnostic> &diagnostics)
{
    std::ifstream input(path);
    if (!input.is_open())
        return false;
    std::ostringstream buffer;
    buffer << input.rdbuf();
    try
    {
        document = json::parse(buffer.str());
    }
    catch (const std::exception &error)
    {
        diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Syntax,
                               path, "文件不是有效 JSON：" + std::string(error.what())});
        return false;
    }
    return true;
}

// 模型注册表结构校验：ModelRegistry 加载是宽容的，这里在写回前把问题挡下来
std::vector<ConfigDiagnostic> validateModelRegistry(const json &document)
{
    std::vector<ConfigDiagnostic> diagnostics;
    if (!document.is_object() || !document.contains("Models") || !document["Models"].is_array())
    {
        diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Missing,
                               "Models", "注册表必须是对象且包含 Models 数组"});
        return diagnostics;
    }

    std::set<std::string> seenNames;
    const json &models = document["Models"];
    for (std::size_t index = 0; index < models.size(); ++index)
    {
        const std::string base = "Models[" + std::to_string(index) + "]";
        const json &provider = models[index];
        if (!provider.is_object())
        {
            diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Type,
                                   base, "供应商必须是对象"});
            continue;
        }

        const json *names = provider.contains("ModelName") ? &provider["ModelName"] : nullptr;
        if (names == nullptr || !names->is_array() || names->empty())
        {
            diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Missing,
                                   base + ".ModelName", "缺少模型名称列表（至少一个）"});
        }
        else
        {
            for (const json &name : *names)
            {
                if (!name.is_string() || name.get<std::string>().empty())
                {
                    diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Type,
                                           base + ".ModelName", "模型名称必须是非空字符串"});
                    continue;
                }
                const std::string modelName = name.get<std::string>();
                if (seenNames.count(modelName) != 0)
                    diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Dependency,
                                           base + ".ModelName",
                                           "模型名称重复：" + modelName});
                seenNames.insert(modelName);
            }
        }

        if (!provider.contains("api_endpoint") || !provider["api_endpoint"].is_string() ||
            provider["api_endpoint"].get<std::string>().empty())
        {
            diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Missing,
                                   base + ".api_endpoint", "缺少 API 端点"});
        }

        const std::string standard = provider.value("APIStandard", "");
        if (standard != "OpenAI" && standard != "Anthropic")
        {
            diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Dependency,
                                   base + ".APIStandard", "API 标准必须是 OpenAI 或 Anthropic"});
        }

        const json &apiKey = provider.contains("api_key") ? provider["api_key"] : json();
        const bool emptyKey = apiKey.is_string() && apiKey.get<std::string>().empty();
        if (emptyKey)
        {
            diagnostics.push_back({ConfigSeverity::Warning, ConfigErrorCategory::Security,
                                   base + ".api_key", "该供应商尚未配置 API Key，调用会失败"});
        }
    }
    return diagnostics;
}

// 兜底清理：current 缺失导致哨兵残留时替换为空值并给出警告
void sanitizeSentinels(json &document, std::vector<ConfigDiagnostic> &warnings)
{
    if (document.is_string() && document.get<std::string>() == kConfigMaskedSentinel)
    {
        document = "";
        warnings.push_back({ConfigSeverity::Warning, ConfigErrorCategory::Security,
                            "$", "密钥值未能从原文件恢复，已置空，请重新填写"});
        return;
    }
    if (document.is_object())
    {
        for (auto &entry : document.items())
            sanitizeSentinels(entry.value(), warnings);
    }
    else if (document.is_array())
    {
        for (json &item : document)
            sanitizeSentinels(item, warnings);
    }
}

std::string errorBody(const std::vector<ConfigDiagnostic> &diagnostics)
{
    return json({{"diagnostics", diagnosticsToArray(diagnostics)}}).dump();
}

bool sleepWhileRunning(const std::atomic<bool> &running, std::chrono::milliseconds duration)
{
    const auto deadline = std::chrono::steady_clock::now() + duration;
    while (running.load() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    return running.load();
}
}

std::unique_ptr<httplib::Server> ConfigPanelServer::buildServer(const WebUiSettings &settings,
                                                                const std::string &configPath,
                                                                ConfigSnapshotStore &store)
{
    auto server = std::make_unique<httplib::Server>();
    const std::shared_ptr<ConfigWriter> writer = std::make_shared<ConfigWriter>();

    server->set_payload_max_length(1024 * 1024);

    server->set_pre_routing_handler(
        [accessToken = settings.accessToken](const httplib::Request &request, httplib::Response &response) {
            // /api/* 统一 Bearer 鉴权；页面本身不含任何数据，可匿名获取
            if (request.path.rfind("/api/", 0) != 0)
                return httplib::Server::HandlerResponse::Unhandled;
            if (!BearerAuth::isAuthorized(request.get_header_value("Authorization"), accessToken))
            {
                response.status = 401;
                response.set_header("WWW-Authenticate", "Bearer");
                response.set_content(R"({"error":"unauthorized"})", "application/json");
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

    server->Get("/", [](const httplib::Request &, httplib::Response &response) {
        // 页面是工作目录下的运行时资源（构建时自动同步到可执行文件旁），按请求现读，
        // 改动刷新浏览器即可生效；no-store 避免调试时端着旧缓存
        std::ifstream input(kPanelPagePath);
        if (!input.is_open())
        {
            LOG_ERROR(std::string("面板页面文件缺失：") + kPanelPagePath +
                      "（应与可执行文件同目录，构建时自动同步）");
            response.status = 500;
            response.set_content("面板页面文件缺失：panel.html 应位于工作目录下，正常构建时会自动同步到可执行文件旁。",
                                 "text/plain; charset=utf-8");
            return;
        }
        std::ostringstream buffer;
        buffer << input.rdbuf();
        response.set_header("Cache-Control", "no-store");
        response.set_content(buffer.str(), "text/html; charset=utf-8");
    });

    server->Get("/api/config", [configPath](const httplib::Request &, httplib::Response &response) {
        json document;
        std::vector<ConfigDiagnostic> diagnostics;
        if (!readJsonFile(configPath, document, diagnostics))
        {
            if (diagnostics.empty())
                diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Source,
                                      configPath, "配置文件无法打开"});
            response.status = 500;
            response.set_content(errorBody(diagnostics), "application/json");
            return;
        }
        response.set_content(maskSecrets(document).dump(), "application/json");
    });

    server->Post("/api/config", [configPath, &store, writer](const httplib::Request &request,
                                                             httplib::Response &response) {
        json candidate;
        try
        {
            candidate = json::parse(request.body);
        }
        catch (const std::exception &error)
        {
            response.status = 400;
            response.set_content(errorBody({{ConfigSeverity::Error, ConfigErrorCategory::Syntax,
                                             "$", std::string("请求体不是有效 JSON：") + error.what()}}),
                                 "application/json");
            return;
        }

        const ConfigWriter::Result written = writer->write(configPath, candidate);
        if (!written.success)
        {
            response.status = 422;
            response.set_content(errorBody(written.diagnostics), "application/json");
            return;
        }

        const ConfigReloadResult reload = store.reload();
        const json body = {{"diff", diffToArray(reload.diff)},
                           {"counts", {{"dynamic", reload.diff.count(ConfigChangeImpact::Dynamic)},
                                       {"rebuild", reload.diff.count(ConfigChangeImpact::Rebuild)},
                                       {"restart", reload.diff.count(ConfigChangeImpact::Restart)}}},
                           {"reloadSuccess", reload.success},
                           {"diagnostics", diagnosticsToArray(reload.diagnostics)},
                           {"warnings", diagnosticsToArray(written.diagnostics)}};
        response.set_content(body.dump(), "application/json");
    });

    server->Get("/api/models", [&store](const httplib::Request &, httplib::Response &response) {
        const std::string registryPath = store.current()->schema->models.registryPath;
        json document;
        std::vector<ConfigDiagnostic> diagnostics;
        if (!readJsonFile(registryPath, document, diagnostics))
        {
            if (!diagnostics.empty())
            {
                // 文件损坏比缺失严重：明确报错而不是当成空注册表
                response.status = 500;
                response.set_content(errorBody(diagnostics), "application/json");
                return;
            }
            const json body = {{"registry_path", registryPath},
                               {"exists", false},
                               {"models", json::object({{"Models", json::array()}})}};
            response.set_content(body.dump(), "application/json");
            return;
        }
        const json body = {{"registry_path", registryPath},
                           {"exists", true},
                           {"models", maskSecrets(document)}};
        response.set_content(body.dump(), "application/json");
    });

    server->Post("/api/models", [&store](const httplib::Request &request, httplib::Response &response) {
        json candidate;
        try
        {
            candidate = json::parse(request.body);
        }
        catch (const std::exception &error)
        {
            response.status = 400;
            response.set_content(errorBody({{ConfigSeverity::Error, ConfigErrorCategory::Syntax,
                                             "$", std::string("请求体不是有效 JSON：") + error.what()}}),
                                 "application/json");
            return;
        }

        const std::string registryPath = store.current()->schema->models.registryPath;
        json current = json::object();
        {
            std::vector<ConfigDiagnostic> diagnostics;
            // false + 有诊断 = 文件损坏，必须拒绝；false + 无诊断 = 文件不存在，允许创建
            if (!readJsonFile(registryPath, current, diagnostics) && !diagnostics.empty())
            {
                response.status = 500;
                response.set_content(errorBody(diagnostics), "application/json");
                return;
            }
        }

        json merged = restoreMaskedSecrets(candidate, current);
        std::vector<ConfigDiagnostic> warnings;
        sanitizeSentinels(merged, warnings);

        const std::vector<ConfigDiagnostic> validation = validateModelRegistry(merged);
        const bool hasErrors = std::any_of(validation.begin(), validation.end(),
                                           [](const ConfigDiagnostic &diagnostic) {
                                               return diagnostic.severity == ConfigSeverity::Error ||
                                                      diagnostic.severity == ConfigSeverity::Fatal;
                                           });
        if (hasErrors)
        {
            response.status = 422;
            response.set_content(errorBody(validation), "application/json");
            return;
        }

        std::vector<ConfigDiagnostic> writeDiagnostics;
        if (!writeJsonAtomically(registryPath, merged, writeDiagnostics))
        {
            response.status = 500;
            response.set_content(errorBody(writeDiagnostics), "application/json");
            return;
        }

        for (const ConfigDiagnostic &diagnostic : validation)
            warnings.push_back(diagnostic);
        const json body = {{"restartRequired", true},
                           {"warnings", diagnosticsToArray(warnings)}};
        response.set_content(body.dump(), "application/json");
    });

    return server;
}

void ConfigPanelServer::run(WebUiSettings settings, std::string configPath,
                            ConfigSnapshotStore &store, const std::atomic<bool> &running)
{
    // 页面是面板唯一入口：缺失时报错并放弃启动，不起一个只会回 500 的空服务；
    // 运行中文件被删的场景仍由 GET / 的 500 分支兜底
    std::ifstream page(kPanelPagePath);
    if (!page.is_open())
    {
        LOG_ERROR(std::string("Web 配置面板未启动：") + kPanelPagePath +
                  " 缺失（应与可执行文件同目录，构建时自动同步），请补回文件后重启");
        return;
    }
    page.close();

    LOG_INFO("Web 配置面板已启动：http://" + settings.bind + ":" +
             std::to_string(settings.port) + "/（访问令牌来自 webui.access_token）");
    while (running.load())
    {
        std::unique_ptr<httplib::Server> server = buildServer(settings, configPath, store);
        std::atomic<bool> serverActive{true};
        // 看门狗：running 置假后调 stop() 让阻塞中的 listen() 返回
        std::thread watchdog([&running, &serverActive, &server]() {
            while (serverActive.load() && running.load())
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            if (!running.load())
                server->stop();
        });

        server->listen(settings.bind.c_str(), static_cast<int>(settings.port));

        serverActive.store(false);
        server->stop();
        watchdog.join();

        if (!running.load())
            break;
        LOG_ERROR("配置面板监听中断（" + settings.bind + ":" + std::to_string(settings.port) +
                  "），10 秒后重试");
        sleepWhileRunning(running, std::chrono::seconds(10));
    }
    LOG_INFO("Web 配置面板已退出");
}

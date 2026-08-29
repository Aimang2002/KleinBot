#include "ConfigPanelServer.h"
#include "ModelCatalog.h"
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
        std::set<std::string> providerNames;
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
                providerNames.insert(modelName);
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

        // Capabilities.vision 双形态：布尔（旧版整组开关）或字符串数组（按模型名单）；
        // 名单出现 ModelName 之外的名称提示疑似拼写错误
        const json *capabilities = nullptr;
        if (provider.contains("Capabilities") && provider["Capabilities"].is_object())
            capabilities = &provider["Capabilities"];
        else if (provider.contains("capabilities") && provider["capabilities"].is_object())
            capabilities = &provider["capabilities"];
        if (capabilities != nullptr && capabilities->contains("vision"))
        {
            const json &vision = (*capabilities)["vision"];
            if (vision.is_array())
            {
                std::set<std::string> listed;
                for (const json &entry : vision)
                {
                    if (!entry.is_string() || entry.get<std::string>().empty())
                    {
                        diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Type,
                                               base + ".Capabilities.vision",
                                               "vision 名单必须是非空字符串"});
                        continue;
                    }
                    listed.insert(entry.get<std::string>());
                }
                for (const std::string &name : listed)
                {
                    if (providerNames.count(name) == 0)
                        diagnostics.push_back({ConfigSeverity::Warning,
                                               ConfigErrorCategory::Dependency,
                                               base + ".Capabilities.vision",
                                               "vision 名单中的模型不在 ModelName 中：" + name});
                }
            }
            else if (!vision.is_boolean())
            {
                diagnostics.push_back({ConfigSeverity::Error, ConfigErrorCategory::Type,
                                       base + ".Capabilities.vision",
                                       "vision 必须是布尔或字符串数组"});
            }
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

// 供应商外呼接口共用的密钥解析：请求显式携带明文优先；
// 掩码哨兵/缺省时按 index 回退注册表中已保存的密钥
std::string resolveProviderApiKey(const json &body, ConfigSnapshotStore &store)
{
    std::string apiKey;
    if (body.contains("api_key") && body["api_key"].is_string())
        apiKey = body["api_key"].get<std::string>();
    if (!apiKey.empty() && apiKey != kConfigMaskedSentinel)
        return apiKey;

    const long index = body.contains("index") && body["index"].is_number_integer()
                           ? body["index"].get<long>()
                           : -1;
    if (index < 0)
        return {};
    const std::string registryPath = store.current()->schema->models.registryPath;
    json registry;
    std::vector<ConfigDiagnostic> diagnostics;
    if (!readJsonFile(registryPath, registry, diagnostics) ||
        !registry.contains("Models") || !registry["Models"].is_array() ||
        index >= static_cast<long>(registry["Models"].size()))
        return {};
    const json &provider = registry["Models"][static_cast<std::size_t>(index)];
    if (provider.contains("api_key") && provider["api_key"].is_string())
        return provider["api_key"].get<std::string>();
    return {};
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

        // 勾选模型会改动 ModelName，而注册表的密钥身份匹配只认数组字段相等，
        // 会把掩码密钥错失回原条目；前端随请求带回每张卡片的原始下标，优先按位恢复。
        // 不带 originIndexes 的旧页面仍走 restoreMaskedSecrets 的身份匹配兜底
        if (candidate.is_object() && candidate.contains("originIndexes") &&
            candidate["originIndexes"].is_array() && candidate.contains("Models") &&
            candidate["Models"].is_array() &&
            candidate["originIndexes"].size() == candidate["Models"].size())
        {
            const json &currentModels = current.value("Models", json::array());
            for (std::size_t index = 0; index < candidate["Models"].size(); ++index)
            {
                const json &origin = candidate["originIndexes"][index];
                json &model = candidate["Models"][index];
                if (!origin.is_number_integer() || !model.is_object() ||
                    !model.contains("api_key") || !model["api_key"].is_string() ||
                    model["api_key"].get<std::string>() != kConfigMaskedSentinel)
                    continue;
                const long originIndex = origin.get<long>();
                if (originIndex < 0 || originIndex >= static_cast<long>(currentModels.size()) ||
                    !currentModels[static_cast<std::size_t>(originIndex)].is_object())
                    continue;
                const json &originModel =
                    currentModels[static_cast<std::size_t>(originIndex)];
                if (originModel.contains("api_key") && originModel["api_key"].is_string())
                    model["api_key"] = originModel["api_key"];
            }
            candidate.erase("originIndexes");
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

    // 面板代为调用供应商的标准模型列表接口，浏览器直连会被 CORS 挡住；
    // 密钥明文可由请求携带（未保存的新供应商），掩码/缺省时回退注册表中已存的密钥
    server->Post("/api/models/available", [&store](const httplib::Request &request,
                                                   httplib::Response &response) {
        json body;
        try
        {
            body = json::parse(request.body);
        }
        catch (const std::exception &error)
        {
            response.status = 400;
            response.set_content(json({{"error", std::string("请求体不是有效 JSON：") + error.what()}})
                                     .dump(),
                                 "application/json");
            return;
        }
        if (!body.is_object())
        {
            response.status = 400;
            response.set_content(json({{"error", "请求体必须是对象"}}).dump(),
                                 "application/json");
            return;
        }

        const std::string endpoint = body.value("api_endpoint", std::string());
        const std::string standard = body.value("APIStandard", std::string());
        const auto reject = [&response](int status, const std::string &message) {
            response.status = status;
            response.set_content(json({{"error", message}}).dump(), "application/json");
        };
        if (endpoint.empty())
        {
            reject(422, "请先填写 API 端点");
            return;
        }
        if (standard != "OpenAI" && standard != "Anthropic")
        {
            reject(422, "API 标准必须是 OpenAI 或 Anthropic");
            return;
        }

        std::string apiKey = resolveProviderApiKey(body, store);
        if (apiKey.empty())
        {
            reject(422, "该供应商尚未配置 API Key，无法拉取模型列表");
            return;
        }

        const std::optional<std::string> modelsUrl = ModelCatalog::deriveModelsUrl(endpoint);
        if (!modelsUrl)
        {
            reject(422, "无法推导模型列表地址：端点应以 /chat/completions 或 /messages 结尾");
            return;
        }

        std::string error;
        const std::optional<std::vector<std::string>> models =
            ModelCatalog::fetch(*modelsUrl, apiKey, standard, 10000, error);
        if (!models)
        {
            reject(502, error);
            return;
        }
        response.set_content(json({{"models", *models}}).dump(), "application/json");
    });

    // 视觉能力探测：向指定模型发一条 1x1 图片 + 极短文字的多模态请求，
    // 按响应把结论分类为 vision / no-vision / unknown（三态，失败不算“不支持”）。
    // 恒返回 200 + result 字段，前端据此渲染，无需按 HTTP 状态区分探测结论
    server->Post("/api/models/check-vision", [&store](const httplib::Request &request,
                                                     httplib::Response &response) {
        json body;
        try
        {
            body = json::parse(request.body);
        }
        catch (const std::exception &error)
        {
            response.status = 400;
            response.set_content(json({{"error", std::string("请求体不是有效 JSON：") + error.what()}})
                                     .dump(),
                                 "application/json");
            return;
        }
        if (!body.is_object())
        {
            response.status = 400;
            response.set_content(json({{"error", "请求体必须是对象"}}).dump(),
                                 "application/json");
            return;
        }

        const std::string endpoint = body.value("api_endpoint", std::string());
        const std::string standard = body.value("APIStandard", std::string());
        const std::string model = body.value("model", std::string());
        const auto reject = [&response](int status, const std::string &message) {
            response.status = status;
            response.set_content(json({{"error", message}}).dump(), "application/json");
        };
        if (model.empty())
        {
            reject(422, "缺少要探测的模型名称");
            return;
        }
        if (endpoint.empty())
        {
            reject(422, "请先填写 API 端点");
            return;
        }
        if (standard != "OpenAI" && standard != "Anthropic")
        {
            reject(422, "API 标准必须是 OpenAI 或 Anthropic");
            return;
        }

        const std::string apiKey = resolveProviderApiKey(body, store);
        if (apiKey.empty())
        {
            reject(422, "该供应商尚未配置 API Key，无法探测");
            return;
        }

        std::string error;
        const std::optional<ModelCatalog::VisionProbe> outcome =
            ModelCatalog::probeVision(endpoint, apiKey, standard, model, 15000, error);
        if (!outcome)
        {
            reject(502, error);
            return;
        }
        const char *result = *outcome == ModelCatalog::VisionProbe::Vision    ? "vision"
                             : *outcome == ModelCatalog::VisionProbe::NoVision ? "no-vision"
                                                                               : "unknown";
        json responseBody = {{"result", result}};
        if (!error.empty())
            responseBody["detail"] = error;
        response.set_content(responseBody.dump(), "application/json");
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

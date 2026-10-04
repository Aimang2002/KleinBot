#include "PushServer.h"
#include "PushContent.h"

#include "../Library/nlohmann/json.hpp"
#include "../Log/Log.h"
#include "../Network/BearerAuth.h"

#include "../../Library/httplib/httplib.h"

#include <chrono>
#include <thread>

namespace
{
using json = nlohmann::json;

// 单请求体上限：留出 6MB 图片 base64 之外的 JSON 结构余量
constexpr std::size_t kMaxPayloadBytes = 10ULL * 1024ULL * 1024ULL;

void errorResponse(httplib::Response &response, int status, const json &body)
{
    response.status = status;
    response.set_content(body.dump(), "application/json");
}

// 解析 {target, contents[], skip_render?}；失败返回 false 并已写好响应
bool parsePushRequest(const httplib::Request &request, httplib::Response &response,
                      PushTarget &target, std::vector<PushContent> &contents, bool &skipRender)
{
    json body;
    try
    {
        body = json::parse(request.body);
    }
    catch (const std::exception &error)
    {
        errorResponse(response, 400, {{"error", "invalid_json"},
                                      {"message", std::string("请求体不是有效 JSON：") + error.what()}});
        return false;
    }
    if (!body.is_object())
    {
        errorResponse(response, 400, {{"error", "invalid_json"}, {"message", "请求体必须是 JSON 对象"}});
        return false;
    }

    static const json kNullJson = nullptr;
    const json &targetJson = body.contains("target") ? body["target"] : kNullJson;
    if (!targetJson.is_object() || !targetJson.contains("type") || !targetJson.contains("id"))
    {
        errorResponse(response, 422,
                      {{"error", "invalid_target"},
                       {"message", "target 必须是 {type: group|private, id: 正整数}"}});
        return false;
    }
    const std::string targetType = targetJson["type"].is_string() ? targetJson["type"].get<std::string>() : "";
    const bool isGroup = targetType == "group";
    if ((!isGroup && targetType != "private") || !targetJson["id"].is_number_integer() ||
        targetJson["id"].get<long long>() <= 0)
    {
        errorResponse(response, 422,
                      {{"error", "invalid_target"},
                       {"message", "target.type 必须是 group|private，target.id 必须是正整数"}});
        return false;
    }
    target = {isGroup, targetJson["id"].get<long long>()};

    const json &contentsJson = body.contains("contents") ? body["contents"] : kNullJson;
    if (!contentsJson.is_array() || contentsJson.empty())
    {
        errorResponse(response, 422,
                      {{"error", "invalid_content"},
                       {"message", "contents 必须是非空数组"},
                       {"supported", json::array({"text", "image"})}});
        return false;
    }
    for (const auto &entry : contentsJson)
    {
        if (!entry.is_object() || !entry.contains("type"))
        {
            errorResponse(response, 422,
                          {{"error", "invalid_content"}, {"message", "每个内容块必须是含 type 的对象"}});
            return false;
        }
        const std::string type = entry["type"].is_string() ? entry["type"].get<std::string>() : "";
        // text 用 data 字段；image 用 data_base64（与对话中演示的契约一致）
        const char *dataKey = type == "image" ? "data_base64" : "data";
        if (!entry.contains(dataKey) || !entry[dataKey].is_string() ||
            entry[dataKey].get<std::string>().empty())
        {
            errorResponse(response, 422,
                          {{"error", "invalid_content"},
                           {"message", std::string("内容块缺少非空字符串字段 ") + dataKey},
                           {"supported", json::array({"text", "image"})}});
            return false;
        }
        std::string mime;
        if (entry.contains("mime") && entry["mime"].is_string())
            mime = entry["mime"].get<std::string>();
        contents.push_back({type, entry[dataKey].get<std::string>(), std::move(mime)});
    }

    skipRender = body.contains("skip_render") && body["skip_render"].is_boolean() &&
                 body["skip_render"].get<bool>();
    return true;
}
}

std::unique_ptr<httplib::Server> PushServer::buildServer(const PushSettings &settings,
                                                        PushDelivery &delivery)
{
    auto server = std::make_unique<httplib::Server>();
    server->set_payload_max_length(kMaxPayloadBytes);

    // /push 是唯一路由，统一 Bearer 鉴权；令牌为空说明配置异常，一律拒绝
    server->set_pre_routing_handler(
        [accessToken = settings.accessToken](const httplib::Request &request, httplib::Response &response) {
            if (request.path != "/push" || request.method != "POST")
            {
                errorResponse(response, 404, {{"error", "not_found"}});
                return httplib::Server::HandlerResponse::Handled;
            }
            if (accessToken.empty() ||
                !BearerAuth::isAuthorized(request.get_header_value("Authorization"), accessToken))
            {
                response.set_header("WWW-Authenticate", "Bearer");
                errorResponse(response, 401, {{"error", "unauthorized"}});
                return httplib::Server::HandlerResponse::Handled;
            }
            return httplib::Server::HandlerResponse::Unhandled;
        });

    server->Post("/push", [&delivery](const httplib::Request &request, httplib::Response &response) {
        PushTarget target;
        std::vector<PushContent> contents;
        bool skipRender = false;
        if (!parsePushRequest(request, response, target, contents, skipRender))
            return;

        const PushReject result = delivery.deliver(target, contents, skipRender);
        switch (result)
        {
        case PushReject::Ok:
            response.set_content(R"({"status":"accepted"})", "application/json");
            break;
        case PushReject::DeniedTarget:
            errorResponse(response, 403, {{"error", "target_not_allowed"}});
            break;
        case PushReject::RateLimited:
            errorResponse(response, 429, {{"error", "rate_limited"}});
            break;
        case PushReject::TextTooLarge:
        case PushReject::ImageTooLarge:
            errorResponse(response, 413, {{"error", "content_too_large"}});
            break;
        case PushReject::UnsupportedType:
            errorResponse(response, 422,
                          {{"error", "unsupported_content_type"},
                           {"supported", json::array({"text", "image"})}});
            break;
        case PushReject::InvalidContent:
        default:
            errorResponse(response, 422, {{"error", "invalid_content"}});
            break;
        }
    });

    return server;
}

void PushServer::run(PushSettings settings, PushDelivery &delivery, const std::atomic<bool> &running)
{
    // 正常路径下加载器已在令牌为空时关闭 enabled；此处防御性再查一次，
    // 不起一个只会回 401 的空服务
    if (settings.accessToken.empty())
    {
        LOG_ERROR("推送门未启动：push.access_token 为空");
        return;
    }
    LOG_INFO("推送门已启动：http://" + settings.bind + ":" + std::to_string(settings.port) +
             "/push（令牌来自 push.access_token）");
    while (running.load())
    {
        std::unique_ptr<httplib::Server> server = buildServer(settings, delivery);
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
        LOG_ERROR("推送门监听中断（" + settings.bind + ":" + std::to_string(settings.port) +
                  "），10 秒后重试");
        for (int waited = 0; waited < 100 && running.load(); ++waited)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    LOG_INFO("推送门已退出");
}

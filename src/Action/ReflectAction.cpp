#include "ReflectAction.h"

#include "../Perception/ReflectionService.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace
{
// 结果只回灌模型、不结束工具轮（terminal=false）：短句是给她看的，回复由她自己说
ActionResult textResult(std::string text)
{
    return {std::move(text), {}, {}, false};
}

std::uint64_t groupArgument(const nlohmann::json &arguments)
{
    const auto it = arguments.find("group");
    if (it == arguments.end())
        return 0;
    if (it->is_number_unsigned())
        return it->get<std::uint64_t>();
    if (it->is_number_integer())
    {
        const std::int64_t value = it->get<std::int64_t>();
        return value > 0 ? static_cast<std::uint64_t>(value) : 0;
    }
    if (it->is_string())
    {
        const std::string text = it->get<std::string>();
        if (text.empty() || text.size() > 19 ||
            !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            return 0;
        return std::stoull(text);
    }
    return 0;
}
} // namespace

ReflectAction::ReflectAction(const Live<AffectOptions> &options) : options(options) {}

const ActionDescriptor &ReflectAction::descriptor() const
{
    static const ActionDescriptor value{
        "reflect_now",
        "让你现在就回想：把你最近在群里的经历（开口说的话、没开口、离场，以及之后大家的反应）想一遍，"
        "写下每次的读法，需要的话增改你的认识。平时每群每 6 小时自动回想一次；部署者让你现在想时用这个。"
        "回想在后台进行，这个工具只是安排上，结果之后用 review_guidance 翻看。"
        "参数 group 是群号；不填就是所有有事可想的群。返回安排了几个群，以及哪些群已经在回想或排着队。",
        {{"type", "object"},
         {"properties",
          {{"group", {{"type", "integer"}, {"description", "群号；不填 = 所有有待回想经历的群"}}}}}},
        true,
        true};
    return value;
}

bool ReflectAction::available() const
{
    return service != nullptr && options.get()->enabled;
}

ActionResult ReflectAction::execute(const nlohmann::json &raw, const ActionContext &)
{
    if (service == nullptr || !options.get()->enabled)
        return textResult("功能未开启");
    const nlohmann::json arguments = raw.is_object() ? raw : nlohmann::json::object();
    const std::uint64_t groupId = groupArgument(arguments);
    const affect::ReflectionRequest request = service->requestNow(groupId);
    if (request.unavailable && request.scheduled == 0 && request.running == 0 && request.waiting == 0)
        return textResult("回想眼下用不了，这次没安排。");
    if (groupId != 0)
    {
        if (request.scheduled > 0)
            return textResult("安排了 1 个群的回想，在后台进行。");
        if (request.running > 0)
            return textResult("这个群正在回想，不用再安排；结果之后用 review_guidance 翻看。");
        if (request.waiting > 0)
            return textResult("这个群的回想已经排上了，正在后台等着，不用再安排。");
        return textResult("这个群眼下没有待回想的经历（最近 24 小时里在那的开口、沉默、离场都回想过了，"
                          "或最近一次还不到 20 分钟）。");
    }
    const std::size_t busy = request.running + request.waiting;
    if (request.scheduled == 0 && busy == 0)
        return textResult("眼下没有待回想的经历。");
    std::string text = request.scheduled > 0
                           ? "安排了 " + std::to_string(request.scheduled) + " 个群的回想，在后台进行。"
                           : std::string("没有新安排的群。");
    if (busy > 0)
        text += "另有 " + std::to_string(busy) + " 个群已经在回想或排着队，不用再安排。";
    return textResult(text);
}

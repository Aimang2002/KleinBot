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
        "让你现在就回想：把你最近在群里说过的话、说完之后大家的反应想一遍，写下每次的读法，"
        "需要的话增改你的认识。平时每群每 6 小时自动回想一次；部署者让你现在想时用这个。"
        "回想在后台进行，这个工具只是安排上，结果之后用 review_guidance 翻看。"
        "参数 group 是群号；不填就是所有有事可想的群。返回安排了几个群。",
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
    const std::size_t scheduled = service->requestNow(groupId);
    if (scheduled == 0)
        return textResult(groupId != 0 ? "这个群眼下没有待回想的经历（她最近没在那说过话，或说完还不到 20 分钟）。"
                                       : "眼下没有待回想的经历。");
    return textResult("安排了 " + std::to_string(scheduled) + " 个群的回想，在后台进行。");
}

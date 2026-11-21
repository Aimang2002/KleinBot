#include "PersonCardAction.h"

#include "../Memory/PersonCard.h"
#include "../Port/ToolNames.h"

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

std::string stringArgument(const nlohmann::json &arguments, const char *field)
{
    const auto it = arguments.find(field);
    if (it == arguments.end() || !it->is_string())
        return {};
    return it->get<std::string>();
}

// 非负整数参数（数字或纯数字串）；没有或不合法返回 -1
long long integerArgument(const nlohmann::json &arguments, const char *field)
{
    const auto it = arguments.find(field);
    if (it == arguments.end() || it->is_null())
        return -1;
    if (it->is_number_unsigned())
        return static_cast<long long>(it->get<std::uint64_t>());
    if (it->is_number_integer())
    {
        const long long value = it->get<long long>();
        return value >= 0 ? value : -1;
    }
    if (it->is_string())
    {
        const std::string text = it->get<std::string>();
        if (text.empty() || text.size() > 18 ||
            !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            return -1;
        return std::stoll(text);
    }
    return -1;
}
} // namespace

UpdatePersonCardAction::UpdatePersonCardAction(PersonCards &cards) : cards(cards) {}

const ActionDescriptor &UpdatePersonCardAction::descriptor() const
{
    static const ActionDescriptor value{
        KleinUpdatePersonCardToolName,
        "对方的卡片：关于某个人的几项固定信息，每次和他说话、或他在群里在场时你都会看到。"
        "这个工具改卡片上你自己记的部分，当场生效，以后一直带着。"
        "slot 有四种：wanted_name 对方希望你怎么叫他；my_name 你自己怎么叫他；name_for_me 他怎么叫你；"
        "note 你觉得关于这个人最该记住的一句（至多 5 条，放满之后要用 index 指明换掉哪一条）。"
        "value 留空表示清掉那一项。user 是那个人的 QQ 号；不填就是正在跟你说话的人。"
        "QQ 号、昵称、是不是部署者由程序给出，改不了。返回改完之后的卡片。",
        {{"type", "object"},
         {"properties",
          {{"slot",
            {{"type", "string"},
             {"enum", {"wanted_name", "my_name", "name_for_me", "note"}},
             {"description", "要改的槽位"}}},
           {"value", {{"type", "string"}, {"description", "一行，至多 40 字；留空 = 清掉"}}},
           {"user", {{"type", "integer"}, {"description", "那个人的 QQ 号；不填 = 正在跟你说话的人"}}},
           {"index", {{"type", "integer"}, {"description", "slot 为 note 时：第几条（1–5）；不填 = 放进空位"}}}}},
         {"required", {"slot", "value"}}},
        true,
        false};
    return value;
}

bool UpdatePersonCardAction::available() const
{
    return cards.enabled();
}

ActionResult UpdatePersonCardAction::execute(const nlohmann::json &raw, const ActionContext &context)
{
    if (!cards.enabled())
        return textResult("功能未开启");
    const nlohmann::json arguments = raw.is_object() ? raw : nlohmann::json::object();
    const long long user = integerArgument(arguments, "user");
    const std::uint64_t target = user > 0 ? static_cast<std::uint64_t>(user) : context.user_id;
    const long long index = integerArgument(arguments, "index");
    const PersonCards::WriteResult result =
        cards.write(target, stringArgument(arguments, "slot"), stringArgument(arguments, "value"),
                    index < 0 ? 0 : static_cast<int>(std::min<long long>(index, 99)),
                    context.user_message_id);
    if (!result.ok)
        return textResult("没改：" + result.error + "。");
    const std::string line =
        renderGroupLine(result.card, target, cards.isDeployer(target), "这个人");
    return textResult(line.empty() ? "改好了，这张卡片现在是空的。" : "改好了，卡片现在是——" + line);
}

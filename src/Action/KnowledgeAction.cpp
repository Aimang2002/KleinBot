#include "KnowledgeAction.h"

#include "../Affect/KnowledgeLedger.h"
#include "../utils/Hash.h"
#include "../utils/LocalDate.h"
#include "../utils/Utils.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace
{
// 结果只回灌模型、不结束工具轮（terminal=false）：终态结果会被原样发出去，
// 而这些短句是给她看的，回复由她用自己的话说
ActionResult textResult(std::string text)
{
    return {std::move(text), {}, {}, false};
}

std::string twoDecimals(double value)
{
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%.2f", value);
    return buffer;
}

std::string timeText(std::int64_t at)
{
    const std::tm local = localTimeOf(static_cast<std::time_t>(at));
    // 64：发布版 -Wformat-truncation 按 int 最大位数估算，32 字节会告警（实际只用 16 字节）
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d %02d:%02d", local.tm_year + 1900,
                  local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min);
    return buffer;
}

std::string statusWord(const std::string &status)
{
    if (status == "active")
        return "有效";
    if (status == "faded")
        return "淡了";
    if (status == "withdrawn")
        return "不作数";
    return status;
}

std::string originWord(const std::string &origin)
{
    if (origin == "teaching")
        return "教导";
    if (origin == "reflection")
        return "回想";
    if (origin == "rollback")
        return "回滚";
    if (origin == "decay")
        return "遗忘";
    return "—";
}

std::string changeWord(const std::string &change)
{
    if (change == "add")
        return "新增";
    if (change == "modify")
        return "改写";
    if (change == "strengthen")
        return "再确认";
    if (change == "weaken")
        return "减弱";
    if (change == "fade")
        return "淡了";
    if (change == "withdraw")
        return "撤回";
    if (change == "rollback")
        return "回滚";
    return change;
}

// 范围的说法与工具参数 scope 的三种写法对得上：全局 / 群 N / 关于 N
std::string scopeWord(const affect::KnowledgeEntry &entry)
{
    if (entry.kind == "person")
        return "关于 " + std::to_string(entry.scopeUser) +
               (entry.scopeGroup == 0 ? std::string() : "·群 " + std::to_string(entry.scopeGroup));
    if (entry.scopeGroup == 0)
        return "全局";
    return "群 " + std::to_string(entry.scopeGroup) + (entry.kind == "group" ? "·群认识" : "");
}

// 认识的 key：教导没有 key，取正文的短哈希——同一句再记一次落在同一条上（算再确认）
std::string teachingKey(const std::string &text)
{
    return "t_" + utils::hmacSha1Hex("affect.teaching", text).substr(0, 12);
}

// 工具参数 scope：global | group:<群号> | user:<QQ号>（冒号中英文都收）
struct GuidanceScope
{
    enum class Kind
    {
        Global,
        Group,
        User,
    };
    Kind kind = Kind::Global;
    std::uint64_t id = 0;
};

std::optional<std::uint64_t> digitsOf(const std::string &text)
{
    if (text.empty() || text.size() > 19 ||
        !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
        return std::nullopt;
    const std::uint64_t value = std::stoull(text);
    if (value == 0)
        return std::nullopt;
    return value;
}

// "group:123" → 123；前缀不对或号码不对返回 nullopt
std::optional<std::uint64_t> idAfter(const std::string &text, const std::string &prefix)
{
    if (text.compare(0, prefix.size(), prefix) != 0)
        return std::nullopt;
    std::string rest = text.substr(prefix.size());
    if (rest.compare(0, 1, ":") == 0)
        rest = rest.substr(1);
    else if (rest.compare(0, 3, "\xEF\xBC\x9A") == 0) // 全角冒号
        rest = rest.substr(3);
    else
        return std::nullopt;
    return digitsOf(utils::trim(rest));
}

std::optional<GuidanceScope> parseScope(const std::string &raw)
{
    const std::string text = utils::trim(raw);
    if (text == "global")
        return GuidanceScope{};
    if (const std::optional<std::uint64_t> group = idAfter(text, "group"))
        return GuidanceScope{GuidanceScope::Kind::Group, *group};
    if (const std::optional<std::uint64_t> user = idAfter(text, "user"))
        return GuidanceScope{GuidanceScope::Kind::User, *user};
    return std::nullopt;
}

const char *const kScopeUsage = "scope 只能是 global、group:<群号> 或 user:<QQ号>。";

// 编号参数：整数，或模型写成字符串的 "3" / "#3"
struct IdArgument
{
    bool present = false;
    bool valid = false;
    std::int64_t value = 0;
};

IdArgument idArgument(const nlohmann::json &arguments, const char *name)
{
    IdArgument result;
    if (!arguments.contains(name) || arguments[name].is_null())
        return result;
    result.present = true;
    const nlohmann::json &value = arguments[name];
    if (value.is_number_integer())
        result.value = value.get<std::int64_t>();
    else if (value.is_string())
    {
        std::string text = utils::trim(value.get<std::string>());
        if (!text.empty() && text.front() == '#')
            text = text.substr(1);
        if (const std::optional<std::uint64_t> digits = digitsOf(text))
            result.value = static_cast<std::int64_t>(*digits);
    }
    result.valid = result.value > 0;
    return result;
}

std::string stringArgument(const nlohmann::json &arguments, const char *name)
{
    if (!arguments.contains(name) || !arguments[name].is_string())
        return std::string();
    return utils::trim(arguments[name].get<std::string>());
}

std::string firstReason(const affect::KnowledgeRunResult &result)
{
    return result.rejected.empty() ? std::string("账本没收") : result.rejected.front().reason;
}

nlohmann::json integerProperty(const char *description)
{
    return {{"type", "integer"}, {"description", description}};
}

nlohmann::json stringProperty(const char *description)
{
    return {{"type", "string"}, {"description", description}};
}
} // namespace

// ---------- 公共部分 ----------

KnowledgeAction::KnowledgeAction(affect::KnowledgeLedger &ledger, const Live<AffectOptions> &options)
    : ledger(ledger), options(options)
{
}

bool KnowledgeAction::available() const
{
    return options.get()->enabled;
}

ActionResult KnowledgeAction::execute(const nlohmann::json &raw, const ActionContext &context)
{
    // 开关关着时工具表里本来就没有它，工具循环按“未开放”拒绝；这里再挡一次，不碰账本
    if (!options.get()->enabled)
        return textResult("功能未开启");
    if (!ledger.open())
        return textResult("账本打不开，这次没有动。");
    const nlohmann::json arguments = raw.is_object() ? raw : nlohmann::json::object();
    if (decayFirst(arguments))
        ledger.applyDecay();
    return run(arguments, context);
}

// ---------- remember_guidance ----------

RememberGuidanceAction::RememberGuidanceAction(affect::KnowledgeLedger &ledger,
    const Live<AffectOptions> &options)
    : KnowledgeAction(ledger, options)
{
}

const ActionDescriptor &RememberGuidanceAction::descriptor() const
{
    static const ActionDescriptor value{
        "remember_guidance",
        "你的认识账本：一句一句关于你以后怎么做的认识。部署者在私聊里对你说的话，你觉得是在说你以后该怎么做的，"
        "可以用这个工具用自己的话记一句；记不记、记成什么由你判断。记下的条目算部署者的教导：不会随时间淡掉，"
        "你事后回想时改不了，只有部署者能让你忘掉。同一句再记一次算再确认；要改写已有的某一条，填 replaces。"
        "返回记下的编号、范围和原文。记了之后用自己的话回复，不要复述格式。",
        {{"type", "object"},
         {"properties",
          {{"text", stringProperty("用你自己的话写的一句，一行，至多 80 字")},
           {"scope", stringProperty("适用范围：global（默认，到处都算）、group:<群号>（只在那个群）、"
                                    "user:<QQ号>（关于那个人）")},
           {"replaces", integerProperty("要改写的条目编号；不填就是新记一条")}}},
         {"required", {"text"}}},
        true,
        true};
    return value;
}

ActionResult RememberGuidanceAction::run(const nlohmann::json &arguments, const ActionContext &context)
{
    const std::string text = stringArgument(arguments, "text");
    const IdArgument replaces = idArgument(arguments, "replaces");
    if (replaces.present && !replaces.valid)
        return textResult("replaces 要填条目编号。");

    affect::KnowledgeChange change;
    change.text = text;
    change.searchText = text;
    affect::KnowledgeRunContext run;
    run.origin = "teaching";
    run.note = {{"by", context.user_id}, {"message_id", context.user_message_id}, {"via", "tool"}};
    if (replaces.present)
    {
        // 按编号改写：范围取那一条自己的；管理员私聊里哪个范围都能改（run.groupId = 0）
        change.op = "modify";
        change.targetId = replaces.value;
    }
    else
    {
        const std::string scopeText = stringArgument(arguments, "scope");
        const std::optional<GuidanceScope> scope =
            scopeText.empty() ? std::optional<GuidanceScope>(GuidanceScope{}) : parseScope(scopeText);
        if (!scope)
            return textResult(kScopeUsage);
        change.op = "add";
        change.key = teachingKey(text);
        switch (scope->kind)
        {
        case GuidanceScope::Kind::Global:
            change.kind = "habit";
            break;
        case GuidanceScope::Kind::Group:
            // 某个群里怎么做 = 那个群的习惯。账本的作用域规则按“本次所在的群”核对，
            // 私聊里记某个群的就把这次运行算在那个群
            change.kind = "habit";
            change.scopeGroup = scope->id;
            run.groupId = scope->id;
            break;
        case GuidanceScope::Kind::User:
            change.kind = "person";
            change.scopeUser = scope->id;
            break;
        }
    }

    const affect::KnowledgeRunResult result = ledger.apply({change}, run);
    if (!result.ok)
        return textResult("账本出了问题，这次没记上。");
    if (result.written.empty())
        return textResult("没记下：" + firstReason(result) + "。");
    const std::int64_t id = result.written.front();
    const std::optional<affect::KnowledgeEntry> entry = ledger.find(id);
    const std::string where = "（#" + std::to_string(id) + "｜" + (entry ? scopeWord(*entry) : "") + "）";
    const std::string stored = entry ? entry->text : text;
    const std::string &applied = result.applied.front();
    if (applied == "add")
        return textResult("记下了" + where + "：" + stored);
    if (applied == "strengthen")
        return textResult("这句本来就记着，算再确认一次" + where + "：" + stored);
    return textResult("改好了" + where + "：" + stored);
}

// ---------- review_guidance ----------

ReviewGuidanceAction::ReviewGuidanceAction(affect::KnowledgeLedger &ledger,
    const Live<AffectOptions> &options)
    : KnowledgeAction(ledger, options)
{
}

const ActionDescriptor &ReviewGuidanceAction::descriptor() const
{
    static const ActionDescriptor value{
        "review_guidance",
        "翻看你的认识账本。不给参数列出全部范围的条目（至多 30 条）；给 scope 只看那个范围；"
        "给 id 看那一条和它的变化。每条有编号、范围、原文、强度、来源、最后确认时间。"
        "结果只给你看，回复时用自己的话说，不要复述格式。",
        {{"type", "object"},
         {"properties",
          {{"scope", stringProperty("global、group:<群号> 或 user:<QQ号>；不填 = 全部")},
           {"id", integerProperty("条目编号")}}}},
        true,
        true};
    return value;
}

ActionResult ReviewGuidanceAction::run(const nlohmann::json &arguments, const ActionContext &)
{
    const auto line = [this](const affect::KnowledgeEntry &entry)
    {
        std::string out = "#" + std::to_string(entry.id) + "｜" + scopeWord(entry) + "｜" + entry.text +
                          "｜强度 " + twoDecimals(ledger.effectiveOf(entry)) + "｜" +
                          originWord(ledger.authorOf(entry.id)) + "｜最后确认 " +
                          timeText(entry.lastConfirmedAt);
        if (entry.status != "active")
            out += "｜" + statusWord(entry.status);
        return out;
    };
    const auto listing = [&line](const std::string &title, const std::vector<affect::KnowledgeEntry> &rows)
    {
        if (rows.empty())
            return textResult(title + "：没有条目。");
        std::string out = title + "（" + std::to_string(rows.size()) + " 条）：";
        const std::size_t shown = std::min(rows.size(), kMaxListedEntries);
        for (std::size_t index = 0; index < shown; ++index)
            out += "\n" + line(rows[index]);
        if (shown < rows.size())
            out += "\n……还有 " + std::to_string(rows.size() - shown) + " 条，按 scope 或 id 再看";
        return textResult(out);
    };

    const IdArgument id = idArgument(arguments, "id");
    if (id.present)
    {
        if (!id.valid)
            return textResult("id 要填条目编号。");
        const std::optional<affect::KnowledgeEntry> entry = ledger.find(id.value);
        if (!entry)
            return textResult("没有这一条。");
        std::string out = line(*entry) + "\n变化（新的在前）：";
        const std::vector<affect::KnowledgeHistoryRow> rows = ledger.history(id.value);
        for (std::size_t index = 0; index < rows.size() && index < kMaxHistoryRows; ++index)
        {
            const affect::KnowledgeHistoryRow &row = rows[index];
            out += "\nv" + std::to_string(row.version) + " " + timeText(row.createdAt) + " " +
                   changeWord(row.change) + "·" + originWord(row.origin) + "｜" + row.text;
        }
        if (rows.size() > kMaxHistoryRows)
            out += "\n……更早的还有 " + std::to_string(rows.size() - kMaxHistoryRows) + " 次";
        return textResult(out);
    }

    const std::string scopeText = stringArgument(arguments, "scope");
    if (!scopeText.empty())
    {
        const std::optional<GuidanceScope> scope = parseScope(scopeText);
        if (!scope)
            return textResult(kScopeUsage);
        affect::KnowledgeQuery filter;
        std::string title;
        switch (scope->kind)
        {
        case GuidanceScope::Kind::Global:
            filter.kind = std::string("habit");
            filter.scopeGroup = 0;
            title = "全局";
            break;
        case GuidanceScope::Kind::Group:
            filter.scopeGroup = scope->id; // 那个群的习惯、群认识与那个群里的人
            title = "群 " + std::to_string(scope->id);
            break;
        case GuidanceScope::Kind::User:
            filter.kind = std::string("person");
            filter.scopeUser = scope->id;
            title = "关于 " + std::to_string(scope->id);
            break;
        }
        return listing(title, ledger.query(filter));
    }

    // 全部范围：全局在前，再按群、按人，同一范围里按编号
    std::vector<affect::KnowledgeEntry> all = ledger.query(affect::KnowledgeQuery{});
    std::stable_sort(all.begin(), all.end(),
                     [](const affect::KnowledgeEntry &a, const affect::KnowledgeEntry &b)
                     {
                         const auto rank = [](const affect::KnowledgeEntry &entry)
                         {
                             return std::make_tuple(entry.kind == "person", entry.scopeGroup,
                                                    entry.scopeUser, entry.id);
                         };
                         return rank(a) < rank(b);
                     });
    if (all.empty())
        return textResult("认识账本里还没有条目。");
    return listing("认识账本", all);
}

// ---------- forget_guidance ----------

ForgetGuidanceAction::ForgetGuidanceAction(affect::KnowledgeLedger &ledger,
    const Live<AffectOptions> &options)
    : KnowledgeAction(ledger, options)
{
}

const ActionDescriptor &ForgetGuidanceAction::descriptor() const
{
    static const ActionDescriptor value{
        "forget_guidance",
        "让认识账本里的条目不再作数。给 id：撤回那一条，变化记录保留。给 scope（group:<群号> 或 "
        "user:<QQ号>）：把那个群或那个人的全部条目连同变化记录一起删掉，不可恢复。id 和 scope 只给一个。"
        "做完用自己的话回复，不要复述格式。",
        {{"type", "object"},
         {"properties",
          {{"id", integerProperty("要撤回的条目编号")},
           {"scope", stringProperty("group:<群号> 或 user:<QQ号>")}}}},
        true,
        true};
    return value;
}

bool ForgetGuidanceAction::decayFirst(const nlohmann::json &arguments) const
{
    return idArgument(arguments, "id").present;
}

ActionResult ForgetGuidanceAction::run(const nlohmann::json &arguments, const ActionContext &context)
{
    const IdArgument id = idArgument(arguments, "id");
    const std::string scopeText = stringArgument(arguments, "scope");
    if (id.present == !scopeText.empty())
        return textResult("id 和 scope 只给一个。");

    if (id.present)
    {
        if (!id.valid)
            return textResult("id 要填条目编号。");
        affect::KnowledgeChange change;
        change.op = "withdraw";
        change.targetId = id.value;
        affect::KnowledgeRunContext run;
        run.origin = "teaching";
        run.note = {{"by", context.user_id}, {"message_id", context.user_message_id}, {"via", "tool"}};
        const affect::KnowledgeRunResult result = ledger.apply({change}, run);
        if (!result.ok)
            return textResult("账本出了问题，这次没有动。");
        if (result.written.empty())
            return textResult("没忘掉：" + firstReason(result) + "。");
        const std::optional<affect::KnowledgeEntry> entry = ledger.find(result.written.front());
        return textResult("#" + std::to_string(result.written.front()) + " 不作数了（变化记录还在）" +
                          (entry ? "：" + entry->text : std::string()));
    }

    const std::optional<GuidanceScope> scope = parseScope(scopeText);
    if (!scope)
        return textResult(kScopeUsage);
    if (scope->kind == GuidanceScope::Kind::Global)
        return textResult("全局的只能按 id 一条一条撤回。");
    const bool person = scope->kind == GuidanceScope::Kind::User;
    const std::size_t removed = person ? ledger.forgetPerson(scope->id) : ledger.forgetGroup(scope->id);
    const std::string who = (person ? "关于 " : "群 ") + std::to_string(scope->id);
    if (removed == 0)
        return textResult(who + " 本来就没有条目。");
    return textResult(who + " 的 " + std::to_string(removed) + " 条删了（连同变化记录）。");
}

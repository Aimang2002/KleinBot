#include "KnowledgeGuard.h"

#include "DecisionStore.h"

#include <algorithm>

namespace affect
{
namespace
{
const char *const kOps[] = {"add", "modify", "strengthen", "weaken", "withdraw"};
const char *const kKinds[] = {"habit", "group", "person"};

bool oneOf(const std::string &value, const char *const *begin, const char *const *end)
{
    return std::any_of(begin, end, [&value](const char *item) { return value == item; });
}

std::vector<std::string> codepointsOf(const std::string &text)
{
    std::vector<std::string> units;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        const unsigned char lead = static_cast<unsigned char>(text[pos]);
        std::size_t length = 1;
        if ((lead & 0xF8) == 0xF0)
            length = 4;
        else if ((lead & 0xF0) == 0xE0)
            length = 3;
        else if ((lead & 0xE0) == 0xC0)
            length = 2;
        if (pos + length > text.size())
            length = 1;
        units.push_back(text.substr(pos, length));
        pos += length;
    }
    return units;
}

bool singleLine(const std::string &text)
{
    return text.find('\n') == std::string::npos && text.find('\r') == std::string::npos;
}

bool blank(const std::string &text)
{
    return text.find_first_not_of(" \t\r\n") == std::string::npos;
}

bool validKey(const std::string &key)
{
    if (key.empty() || key.size() > KnowledgeRules::kMaxKeyBytes)
        return false;
    return std::none_of(key.begin(), key.end(), [](char c)
                        { return c == ' ' || c == '\t' || c == '\n' || c == '\r' ||
                                 static_cast<unsigned char>(c) < 0x20; });
}
} // namespace

bool KnowledgeGuard::scopeLegal(const std::string &kind, std::uint64_t scopeGroup,
                                std::uint64_t scopeUser, const KnowledgeRunContext &context,
                                std::string &reason)
{
    const bool groupIsHereOrGlobal =
        scopeGroup == 0 || (context.groupId != 0 && scopeGroup == context.groupId);
    if (kind == "habit")
    {
        if (scopeUser != 0 || !groupIsHereOrGlobal)
        {
            reason = "习惯只能是全局的或本群的";
            return false;
        }
        return true;
    }
    if (kind == "group")
    {
        if (scopeUser != 0 || context.groupId == 0 || scopeGroup != context.groupId)
        {
            reason = "群认识只能写本群";
            return false;
        }
        return true;
    }
    if (kind == "person")
    {
        if (scopeUser == 0 || !groupIsHereOrGlobal)
        {
            reason = "对人的认识要写明是谁，且只能是全局的或本群的";
            return false;
        }
        if (context.origin == "reflection" && context.presentUsers.count(scopeUser) == 0)
        {
            reason = "这个人不在本次材料里";
            return false;
        }
        return true;
    }
    reason = "kind 只能是 habit / group / person";
    return false;
}

bool KnowledgeGuard::quotesMaterial(const std::string &text, const std::vector<std::string> &material)
{
    const std::vector<std::string> units = codepointsOf(text);
    if (units.size() < kQuoteRunCodepoints)
        return false;
    for (const std::string &line : material)
    {
        if (line.size() < kQuoteRunCodepoints)
            continue;
        for (std::size_t start = 0; start + kQuoteRunCodepoints <= units.size(); ++start)
        {
            std::string run;
            for (std::size_t offset = 0; offset < kQuoteRunCodepoints; ++offset)
                run += units[start + offset];
            if (line.find(run) != std::string::npos)
                return true;
        }
    }
    return false;
}

bool KnowledgeGuard::check(const KnowledgeChange &change, const KnowledgeRunContext &context,
                           std::string &reason)
{
    if (context.origin != "reflection" && context.origin != "teaching")
    {
        reason = "来源只能是回想或教导";
        return false;
    }
    if (!oneOf(change.op, std::begin(kOps), std::end(kOps)))
    {
        reason = "op 只能是 add / modify / strengthen / weaken / withdraw";
        return false;
    }
    if (change.targetId == 0)
    {
        if (!oneOf(change.kind, std::begin(kKinds), std::end(kKinds)))
        {
            reason = "kind 只能是 habit / group / person";
            return false;
        }
        if (!validKey(change.key))
        {
            reason = "key 不能为空、不能含空白、不超过 64 字节";
            return false;
        }
        if (!scopeLegal(change.kind, change.scopeGroup, change.scopeUser, context, reason))
            return false;
    }
    else if (change.targetId < 0)
    {
        reason = "编号不对";
        return false;
    }

    const bool writesText = change.op == "add" || change.op == "modify";
    if (writesText)
    {
        if (blank(change.text))
        {
            reason = "正文不能为空";
            return false;
        }
        if (!singleLine(change.text))
        {
            reason = "正文只能是一行";
            return false;
        }
        if (codepointsOf(change.text).size() > KnowledgeRules::kMaxTextCodepoints)
        {
            reason = "正文超过 80 个字";
            return false;
        }
    }
    if (!singleLine(change.searchText) ||
        codepointsOf(change.searchText).size() > KnowledgeRules::kMaxSearchTextCodepoints)
    {
        reason = "检索词只能是一行、不超过 120 个字";
        return false;
    }
    if (!context.material.empty() &&
        (quotesMaterial(change.text, context.material) ||
         quotesMaterial(change.searchText, context.material)))
    {
        reason = "照抄了群聊原文";
        return false;
    }

    if (context.origin == "reflection" && change.evidence.empty())
    {
        reason = "回想的改动必须带依据";
        return false;
    }
    for (const std::string &id : change.evidence)
    {
        const bool shaped = id.rfind("decision:", 0) == 0 || id.rfind("feedback:", 0) == 0;
        if (!shaped)
        {
            reason = "依据只能是 decision:<id> 或 feedback:<id>";
            return false;
        }
        if (!context.evidenceExists || !context.evidenceExists(id))
        {
            reason = "依据不存在";
            return false;
        }
    }
    return true;
}

std::function<bool(const std::string &)> evidenceCheckerFor(const DecisionStore &store)
{
    return [&store](const std::string &id)
    {
        static const std::string kDecision = "decision:";
        static const std::string kFeedback = "feedback:";
        if (id.rfind(kDecision, 0) == 0)
            return store.findDecision(id.substr(kDecision.size())).has_value();
        if (id.rfind(kFeedback, 0) == 0)
            return store.feedbackExists(id.substr(kFeedback.size()));
        return false;
    };
}
} // namespace affect

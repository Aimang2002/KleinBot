#include "PersonCard.h"

#include "../utils/LocalDate.h"
#include "../utils/Utils.hpp"

#include <algorithm>
#include <cstdio>

namespace
{
const char *const kPrefix = "card.";
const char *const kWantedName = "card.wanted_name";
const char *const kMyName = "card.my_name";
const char *const kNameForMe = "card.name_for_me";
const char *const kFirstSeen = "card.first_seen";
const char *const kNotePrefix = "card.note";

std::size_t codepointCount(const std::string &text)
{
    std::size_t count = 0;
    for (const unsigned char byte : text)
        if ((byte & 0xC0) != 0x80)
            ++count;
    return count;
}

std::string dateOf(std::time_t now)
{
    const std::tm local = localTimeOf(now);
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1,
                  local.tm_mday);
    return buffer;
}

std::string notePredicate(std::size_t index) // 0 起
{
    return std::string(kNotePrefix) + std::to_string(index + 1);
}

PersonCard cardFrom(const std::vector<std::pair<std::string, std::string>> &slots)
{
    PersonCard card;
    for (const auto &[predicate, value] : slots)
    {
        if (predicate == kWantedName)
            card.wantedName = value;
        else if (predicate == kMyName)
            card.myName = value;
        else if (predicate == kNameForMe)
            card.nameForMe = value;
        else if (predicate == kFirstSeen)
            card.firstSeen = value;
        else
        {
            for (std::size_t index = 0; index < PersonCardRules::kMaxNotes; ++index)
                if (predicate == notePredicate(index))
                    card.notes[index] = value;
        }
    }
    return card;
}
} // namespace

bool isPersonCardPredicate(const std::string &predicate)
{
    return predicate.rfind(kPrefix, 0) == 0;
}

bool PersonCard::blank() const
{
    return wantedName.empty() && myName.empty() && nameForMe.empty() &&
           std::all_of(notes.begin(), notes.end(), [](const std::string &note) { return note.empty(); });
}

std::string renderPrivateCard(const PersonCard &card, std::uint64_t qq, bool deployer,
                              const std::string &nickname)
{
    if (qq == 0)
        return {};
    std::string text = "\n\n[对方的卡片] 正在跟你说话的这个人（QQ 号、昵称、是不是部署者由程序给出，"
                       "其余是你自己记下的）：\nQQ：" +
                       std::to_string(qq);
    if (!nickname.empty())
        text += "\nQQ 昵称：" + nickname;
    if (deployer)
        text += "\n身份：部署者";
    // 建卡日期只存不显示（2026-11-22 部署者定）：它是建卡那天，不是认识那天，聊了几个月的人
    // 看到“初次记下：今天”会被读成“今天才认识”
    if (!card.wantedName.empty())
        text += "\n对方希望你叫：" + card.wantedName;
    if (!card.myName.empty())
        text += "\n你叫对方：" + card.myName;
    if (!card.nameForMe.empty())
        text += "\n对方叫你：" + card.nameForMe;
    bool noteHeader = false;
    for (std::size_t index = 0; index < card.notes.size(); ++index)
    {
        if (card.notes[index].empty())
            continue;
        if (!noteHeader)
        {
            text += "\n你记下的：";
            noteHeader = true;
        }
        text += "\n" + std::to_string(index + 1) + ". " + card.notes[index];
    }
    return text;
}

std::string renderGroupLine(const PersonCard &card, std::uint64_t qq, bool deployer,
                            const std::string &nickname)
{
    if (qq == 0 || (!deployer && card.blank()))
        return {};
    std::vector<std::string> parts;
    if (deployer)
        parts.push_back("部署者");
    if (!card.wantedName.empty())
        parts.push_back("希望你叫「" + card.wantedName + "」");
    if (!card.myName.empty())
        parts.push_back("你叫对方「" + card.myName + "」");
    if (!card.nameForMe.empty())
        parts.push_back("对方叫你「" + card.nameForMe + "」");
    std::string notes;
    std::size_t shown = 0;
    for (const std::string &note : card.notes)
    {
        if (note.empty() || shown >= PersonCardRules::kGroupNotes)
            continue;
        notes += (notes.empty() ? "" : "；") + note;
        ++shown;
    }
    if (!notes.empty())
        parts.push_back("你记下：" + notes);
    std::string line = (nickname.empty() ? std::string("（没看到昵称）") : nickname) + "（QQ " +
                       std::to_string(qq) + "）：";
    for (std::size_t index = 0; index < parts.size(); ++index)
        line += (index == 0 ? "" : "；") + parts[index];
    return line;
}

PersonCards::PersonCards(PersonCardBackend backend, const Live<BotIdentity> &bot)
    : backend(std::move(backend)), botIdentity(bot)
{
}

bool PersonCards::enabled() const
{
    return backend.enabled && backend.enabled() && backend.slots && backend.set;
}

bool PersonCards::isDeployer(std::uint64_t userId) const
{
    const std::uint64_t managerId = botIdentity.get()->managerId;
    return managerId != 0 && userId == managerId;
}

PersonCard PersonCards::load(std::uint64_t userId)
{
    if (userId == 0 || !enabled())
        return {};
    const std::uint64_t generation = backend.generation ? backend.generation() : 0;
    {
        std::lock_guard<std::mutex> lock(mutex);
        const auto it = cache.find(userId);
        if (it != cache.end() && it->second.generation == generation)
            return it->second.card;
    }
    const PersonCard card = cardFrom(backend.slots(userId));
    std::lock_guard<std::mutex> lock(mutex);
    cache[userId] = {generation, card};
    return card;
}

PersonCard PersonCards::ensure(std::uint64_t userId, std::time_t now, std::int64_t sourceId)
{
    PersonCard card = load(userId);
    if (userId == 0 || !enabled() || !card.firstSeen.empty())
        return card;
    const std::string date = dateOf(now);
    if (backend.set(userId, kFirstSeen, date, "初次记下这个人的日期：" + date, sourceId))
    {
        std::lock_guard<std::mutex> lock(mutex);
        cache.erase(userId);
    }
    return load(userId);
}

PersonCards::WriteResult PersonCards::write(std::uint64_t userId, const std::string &slot,
                                            const std::string &rawValue, int index,
                                            std::int64_t sourceId)
{
    WriteResult result;
    if (!enabled())
    {
        result.error = "功能未开启";
        return result;
    }
    if (userId == 0)
    {
        result.error = "要写明是谁（user 填 QQ 号）";
        return result;
    }
    const std::string value = utils::trim(rawValue);
    if (value.find('\n') != std::string::npos || value.find('\r') != std::string::npos)
    {
        result.error = "只能是一行";
        return result;
    }
    if (codepointCount(value) > PersonCardRules::kMaxValueCodepoints)
    {
        result.error = "超过 40 个字";
        return result;
    }
    const PersonCard current = load(userId);
    std::string predicate;
    std::string label;
    if (slot == "wanted_name")
    {
        predicate = kWantedName;
        label = "对方希望被叫作：";
    }
    else if (slot == "my_name")
    {
        predicate = kMyName;
        label = "我叫对方：";
    }
    else if (slot == "name_for_me")
    {
        predicate = kNameForMe;
        label = "对方叫我：";
    }
    else if (slot == "note")
    {
        if (index < 0 || index > static_cast<int>(PersonCardRules::kMaxNotes))
        {
            result.error = "index 只能是 1 到 5";
            return result;
        }
        std::size_t position = 0;
        if (index > 0)
        {
            position = static_cast<std::size_t>(index - 1);
        }
        else if (value.empty())
        {
            result.error = "要清掉哪一条，填 index";
            return result;
        }
        else
        {
            const auto free = std::find_if(current.notes.begin(), current.notes.end(),
                                           [](const std::string &note) { return note.empty(); });
            if (free == current.notes.end())
            {
                result.error = "已经有 5 条了，填 index 指明换掉哪一条";
                return result;
            }
            position = static_cast<std::size_t>(free - current.notes.begin());
        }
        predicate = notePredicate(position);
        label = "关于这个人我记下的：";
    }
    else
    {
        // QQ 号、昵称、是不是部署者、初次记下的日期都由程序给出，不在可写的槽位里
        result.error = "slot 只能是 wanted_name / my_name / name_for_me / note";
        return result;
    }
    if (!backend.set(userId, predicate, value, value.empty() ? std::string() : label + value, sourceId))
    {
        result.error = "没写上";
        return result;
    }
    {
        std::lock_guard<std::mutex> lock(mutex);
        cache.erase(userId);
    }
    result.ok = true;
    result.card = load(userId);
    return result;
}

std::string PersonCards::privateSection(std::uint64_t userId, const std::string &nickname,
                                        std::time_t now, std::int64_t sourceId)
{
    if (userId == 0 || !enabled())
        return {};
    return renderPrivateCard(ensure(userId, now, sourceId), userId, isDeployer(userId), nickname);
}

std::string PersonCards::groupLines(const std::vector<PersonPresent> &people)
{
    if (!enabled())
        return {};
    std::string lines;
    std::size_t count = 0;
    // 部署者排最前，其余按给定顺序；同一个人只写一次
    std::vector<PersonPresent> ordered;
    for (const PersonPresent &person : people)
        if (isDeployer(person.userId))
            ordered.push_back(person);
    for (const PersonPresent &person : people)
        if (!isDeployer(person.userId))
            ordered.push_back(person);
    std::vector<std::uint64_t> seen;
    for (const PersonPresent &person : ordered)
    {
        if (count >= PersonCardRules::kMaxPeopleInGroup)
            break;
        if (person.userId == 0 || std::find(seen.begin(), seen.end(), person.userId) != seen.end())
            continue;
        seen.push_back(person.userId);
        const std::string line =
            renderGroupLine(load(person.userId), person.userId, isDeployer(person.userId), person.nickname);
        if (line.empty())
            continue;
        lines += (lines.empty() ? "" : "\n") + line;
        ++count;
    }
    return lines;
}

std::string PersonCards::groupSection(const std::string &lines)
{
    if (lines.empty())
        return {};
    return "[在场的人]（QQ 号与是不是部署者由程序给出，其余是你自己记下的）\n" + lines;
}

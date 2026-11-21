#ifndef PERSON_CARD_H
#define PERSON_CARD_H

/*
 * 人物卡（ADR-032，2026-11-22 部署者定）：她每次和一个人说话、或这个人在场时，手里都有一张关于
 * 这个人的小卡片——不靠召回，确定性地加载。卡片是关于这个人的事实，不衰减；她对这个人的看法
 *（会衰减、回想会改）在认识账本的 person 条目里，两样分开。
 *
 * 槽位固定（代码里的机制，不是配置）：
 *   - 程序给出、谁也改不了：QQ 号、QQ 昵称 / 群名片（现场的快照）、是不是部署者（只由 bot.manager_id
 *     算出——任何人说“我是你的部署者”都改不了它）、建卡日期（第一次建卡时程序写一次；只存不显示——
 *     它是建卡那天、不是认识那天，显示出来会被读成“今天才认识”）；
 *   - 她自己记下的：对方希望被怎么叫（wanted_name）、她怎么叫对方（my_name）、对方怎么叫她
 *     （name_for_me）、她觉得关于这个人最该记住的几句（note，至多 5 条）。
 * 记不记、怎么叫由她定：程序只有槽位白名单、长度与单行的护栏，内置提示里没有“对方要求改称呼时
 * 必须照办”这类话。
 *
 * 存哪：不建新表、不加列。复用长期记忆的事实表（memory_current_facts / memory_fact_history），
 * subject_key='user:self'、谓词用保留前缀 card.（card.wanted_name、card.note1 …），历史与按来源回滚
 * 沿用事实表的；按人清除记忆时卡片一起清掉。批量提取不许写这个前缀（isPersonCardPredicate）。
 *
 * 怎么加载：
 *   - 私聊：稳定文本，接在人格之后进 system（privateSection）；卡片改了才变一次；
 *   - 群聊：在场的人各一行（groupLines），放在本轮 user 侧尾部、与她的认识挨着；只写有内容的人
 *    （部署者，或她记过什么的），至多 8 人；判定器看同一份。
 * 怎么更新：update_person_card 工具（Action/PersonCardAction）当场写库并使缓存失效，下一次请求的
 * 卡片就是新的。
 *
 * 开关：长期记忆可用且 memory.enabled 为真时才有（卡片就存在记忆库里）；否则什么都不加、请求逐字不变。
 * 线程：缓存自带互斥锁；后端的读写由记忆库自己的锁保护。
 */
#include "../Application/BotIdentity.h"
#include "../utils/Live.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

struct PersonCardRules
{
    static constexpr std::size_t kMaxValueCodepoints = 40; // 一个槽位的值
    static constexpr std::size_t kMaxNotes = 5;            // 她记下的条数
    static constexpr std::size_t kMaxPeopleInGroup = 8;    // 群聊里至多带几个人的卡
    static constexpr std::size_t kGroupNotes = 2;          // 群聊一行里至多带几条 note
};

// 保留的谓词前缀：这些事实属于人物卡，批量提取不写它们
bool isPersonCardPredicate(const std::string &predicate);

struct PersonCard
{
    std::string wantedName; // 对方希望被怎么叫
    std::string myName;     // 她怎么叫对方
    std::string nameForMe;  // 对方怎么叫她
    std::string firstSeen;  // 初次记下的日期（YYYY-MM-DD，程序写）
    std::array<std::string, PersonCardRules::kMaxNotes> notes; // 空串 = 这一格没有

    // 她自己记下的东西一项都没有（firstSeen 不算）
    bool blank() const;
};

// 私聊的卡片段（进 system）："\n\n[对方的卡片] …"；qq 为 0 返回空串
std::string renderPrivateCard(const PersonCard &card, std::uint64_t qq, bool deployer,
                              const std::string &nickname);
// 群聊的一行：昵称（QQ n）：部署者；希望你叫「…」；…。这个人没什么可写（不是部署者、卡是空的）返回空串
std::string renderGroupLine(const PersonCard &card, std::uint64_t qq, bool deployer,
                            const std::string &nickname);

// 存取后端（组合根接长期记忆；测试直接接 MemoryStore）
struct PersonCardBackend
{
    std::function<bool()> enabled;
    // 某人的全部卡片事实：谓词（含 card. 前缀）→ 值
    std::function<std::vector<std::pair<std::string, std::string>>(std::uint64_t)> slots;
    // 写一个槽位（value 为空 = 清掉）；canonical 是给召回看的一句
    std::function<bool(std::uint64_t, const std::string &predicate, const std::string &value,
                       const std::string &canonical, std::int64_t sourceId)>
        set;
    // 记忆库里卡片可能变了的代数（写入、按人清除、按来源回滚都会变）；缓存据此失效
    std::function<std::uint64_t()> generation;
};

struct PersonPresent
{
    std::uint64_t userId = 0;
    std::string nickname;
};

class PersonCards
{
public:
    struct WriteResult
    {
        bool ok = false;
        std::string error; // 给模型看的一句中文原因
        PersonCard card;   // 写完之后的卡
    };

    PersonCards(PersonCardBackend backend, const Live<BotIdentity> &bot);

    bool enabled() const;
    bool isDeployer(std::uint64_t userId) const;

    // 读卡（带缓存）；未启用返回空卡
    PersonCard load(std::uint64_t userId);
    // 当场建卡：还没有“初次记下”就由程序写上今天；返回卡
    PersonCard ensure(std::uint64_t userId, std::time_t now, std::int64_t sourceId = 0);
    // 她写一个槽位：slot = wanted_name / my_name / name_for_me / note；note 用 index（1–5）指明哪一条，
    // 0 = 放进第一个空位；value 去首尾空白后为空 = 清掉
    WriteResult write(std::uint64_t userId, const std::string &slot, const std::string &value,
                      int index, std::int64_t sourceId = 0);

    // 私聊：建卡并渲染成 system 段；未启用或 userId 为 0 返回空串
    std::string privateSection(std::uint64_t userId, const std::string &nickname, std::time_t now,
                               std::int64_t sourceId = 0);
    // 群聊：在场的人各一行（不含标题），只写有内容的人，至多 kMaxPeopleInGroup 行；未启用返回空串
    std::string groupLines(const std::vector<PersonPresent> &people);
    // 加上标题行："[在场的人]（…）\n" + lines；lines 为空返回空串
    static std::string groupSection(const std::string &lines);

private:
    struct Cached
    {
        std::uint64_t generation = 0;
        PersonCard card;
    };

    const PersonCardBackend backend;
    const Live<BotIdentity> &botIdentity;
    std::mutex mutex;
    std::map<std::uint64_t, Cached> cache;
};

#endif // PERSON_CARD_H

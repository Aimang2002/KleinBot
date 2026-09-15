#include <gtest/gtest.h>

// 真机冒烟（人工执行，不属于常规验证门）：
//   cd <运行时目录（含 source/）>
//   KLEIN_SMOKE_CONFIG=.config.json <kleinbot_tests> --gtest_filter='EngagementSmoke*'
// 用真实 LLM 走通话题会话全链路；用户会话/观察状态/群内容全部落临时目录，
// 绝不读写真实 source/.conversations.db。未设置 KLEIN_SMOKE_CONFIG 时整组跳过
// （与 WebFetch/Tavily 集成测试同款模式）。

#include "Bootstrap/RuntimeSettings.h"
#include "ChatService/ChatService.h"
#include "Configuration/ConfigLoader.h"
#include "Log/Log.h"
#include "Memory/MemoryService.h"
#include "ModelRegistry/ModelRegistry.h"
#include "Network/OneBotApiChannel.h"
#include "Perception/EngagementService.h"
#include "Perception/GroupContextService.h"
#include "Perception/GroupContextStore.h"
#include "Perception/GroupListService.h"
#include "Port/InboundMessage.h"
#include "Port/MessageSenderPort.h"
#include "Persistence/ConversationStore.h"
#include "UserSession/UserSessionService.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
std::string smokeOut()
{
    static const char *out = std::getenv("KLEIN_SMOKE_CONFIG");
    return out == nullptr ? std::string{} : std::string(out);
}

class SmokeApiChannel final : public OneBotApiChannel
{
public:
    OneBotApiResult call(const std::string &, nlohmann::json, std::chrono::milliseconds) override
    {
        return {};
    }
};

class SmokeSender final : public MessageSenderPort
{
public:
    void deliver(OutboundDelivery delivery) override
    {
        const auto *target = std::get_if<GroupMessageTarget>(&delivery.target);
        const auto *text = std::get_if<TextMessage>(&delivery.message);
        const std::string line = "→ 群" + (target != nullptr ? target->group_id : "?") + ": " +
                                 (text != nullptr ? text->content : "<非文本>");
        std::cout << "    [出站] " << line << std::endl;
        texts.push_back(text != nullptr ? text->content : "");
    }
    std::vector<std::string> texts;
};

// 共享的重资产（真实配置 + 真实模型调用链），整个进程只建一次
struct SmokeWorld
{
    RuntimeSettings settings;
    std::unique_ptr<ConversationStore> conversations;
    std::unique_ptr<UserSessionService> userSession;
    std::unique_ptr<MemoryService> memory;
    std::unique_ptr<ModelRegistry> models;
    std::unique_ptr<Dock> dock;
    std::unique_ptr<ChatService> chat;
    std::filesystem::path tempDir;

    static const SmokeWorld &get()
    {
        static SmokeWorld world;
        return world;
    }

    SmokeWorld()
    {
        tempDir = std::filesystem::temp_directory_path() /
                  ("klein-smoke-" + std::to_string(std::time(nullptr)));
        std::filesystem::create_directories(tempDir);

        ConfigLoader loader;
        ConfigLoadResult loadResult = loader.loadFile(smokeOut());
        if (!loadResult.canStart())
            return;
        settings = buildRuntimeSettings(*loadResult.config);
        settings.memory.enabled = false; // 冒烟不烧记忆提取
        settings.storage.conversationDatabase = (tempDir / "conversations.db").string();

        models = std::make_unique<ModelRegistry>(kModelRegistryPath);
        conversations = std::make_unique<ConversationStore>(settings.storage.conversationDatabase);
        userSession = std::make_unique<UserSessionService>(*models, *conversations,
                                                           settings.bot, settings.chat,
                                                           "source/soul.md");
        userSession->ensureUserExists(settings.bot.managerId);
        memory = std::make_unique<MemoryService>(settings.storage.conversationDatabase,
                                                 *conversations, needDock(), *models,
                                                 settings.memory);
        userSession->setMemoryService(memory.get());

        static ToolRegistry emptyTools;
        chat = std::make_unique<ChatService>(memoryDock(), *userSession, *models, emptyTools,
                                             *memory, settings.chat, settings.bot.managerId);
        std::cout << "[冒烟] 初始化完成：默认模型 " << settings.chat.defaultModel
                  << "，临时库 " << tempDir << std::endl;
    }

private:
    // MemoryService/ChatService 都持 Dock 引用，用同一份
    Dock &needDock()
    {
        if (dock == nullptr)
            dock = std::make_unique<Dock>(settings.dock, nullptr);
        return *dock;
    }
    Dock &memoryDock() { return needDock(); }
};

struct Scenario  // 每个场景独立的轻资产
{
    std::uint64_t groupId;
    std::unique_ptr<GroupContextStore> store;
    std::unique_ptr<GroupListService> state;
    std::unique_ptr<GroupContextService> context;
    std::unique_ptr<EngagementService> engagement;
    std::unique_ptr<SmokeSender> sender;
    std::vector<std::string> judgeVerdicts;
    std::vector<std::string> agentMaterials;   // 主模型每次请求的材料首部
    std::vector<std::string> workerUsers;      // 杂务模型每次请求
    std::vector<std::uint64_t> turnSubmissions;
    std::int64_t now = std::time(nullptr);
    std::uint64_t messageIdSeq = 500000;
    // 模拟 KeyedTaskScheduler 的异步语义：任务先入队，drain() 统一执行
    // （内联执行会与 onGroupMessage 的会话锁自死锁）
    std::vector<std::pair<int, std::uint64_t>> pendingTasks;

    explicit Scenario(std::uint64_t group, const std::string &tag)
        : groupId(group),
          store(std::make_unique<GroupContextStore>(
              (SmokeWorld::get().tempDir / (tag + ".db")).string())),
          sender(std::make_unique<SmokeSender>())
    {
        static SmokeApiChannel api;
        state = std::make_unique<GroupListService>(
            (SmokeWorld::get().tempDir / (tag + "-state.db")).string(), api);
        state->setFeatureEnabled(true);
        state->setMonitored(group, true);

        const SmokeWorld &world = SmokeWorld::get();
        context = std::make_unique<GroupContextService>(state.get(), world.settings.bot,
                                                        store.get(), sender.get());
        engagement = std::make_unique<EngagementService>(
            world.settings.bot, store.get(), sender.get(), [this] { return now; });
        context->setEngagement(engagement.get());
        context->setSummarizer([&world](const std::string &s, const std::string &u)
                               { return world.chat->buildOnce(s, u); });
        engagement->setWorker([&world, this](const std::string &s, const std::string &u)
                              {
                                  workerUsers.push_back(u);
                                  return world.chat->buildOnceWith(
                                      world.settings.models.worker, s, u);
                              });
        engagement->setMainAgent(
            [&world, this](const std::string &note, const std::vector<ChatMessage> &history,
                           const std::vector<std::string> &schemas)
            {
                if (!history.empty())
                    agentMaterials.push_back(history.front().content);
                return world.chat->requestInCharacter(world.settings.bot.managerId, note,
                                                      history, schemas);
            });
        engagement->setSubmitTurn([this](std::uint64_t group)
                                  {
                                      turnSubmissions.push_back(group);
                                      pendingTasks.push_back({0, group});
                                  });
        engagement->setSubmitJudge([this](std::uint64_t group)
                                   { pendingTasks.push_back({1, group}); });
    }

    void drain()
    {
        for (int guard = 0; guard < 12 && !pendingTasks.empty(); ++guard)
        {
            const auto [kind, group] = pendingTasks.front();
            pendingTasks.erase(pendingTasks.begin());
            if (kind == 1)
                engagement->runJudge(group);
            else
                engagement->runTurn(group);
        }
        pendingTasks.clear();
    }

    InboundMessage chat(std::uint64_t user, const std::string &nick, const std::string &text,
                        bool atBot = false, const std::string &replyTo = "")
    {
        InboundMessage message;
        message.post_type = "message";
        message.message_type = "group";
        message.user_id = user;
        message.group_id = groupId;
        message.nickname = nick;
        message.plain_text = text;
        message.message_timestamp = now;
        message.message_id = ++messageIdSeq;
        message.message_id_raw = "smoke" + std::to_string(message.message_id);
        if (atBot)
            message.mentioned_ids.push_back(SmokeWorld::get().settings.bot.id);
        message.reply_to_message_id_raw = replyTo;
        return message;
    }

    void feed(std::uint64_t user, const std::string &nick, const std::string &text,
              bool atBot = false, const std::string &replyTo = "")
    {
        std::cout << "  [" << nick << "] " << text.substr(0, 60)
                  << (text.size() > 60 ? "…" : "") << std::endl;
        context->observe(chat(user, nick, text, atBot, replyTo));
        drain();
    }
};

std::string repeatText(const std::string &unit, int times)
{
    std::string text;
    for (int index = 0; index < times; ++index)
        text += unit;
    return text;
}
} // namespace

// ===== S0：API 连通性 ping（最小时延路径） =====
TEST(EngagementSmoke, S0_ApiPing)
{
    if (smokeOut().empty())
        GTEST_SKIP() << "KLEIN_SMOKE_CONFIG is not configured";
    const SmokeWorld &world = SmokeWorld::get();
    std::cout << "\n===== S0 API ping =====" << std::endl;
    const auto start = std::time(nullptr);
    const std::string reply = world.chat->buildOnceWith(
        world.settings.models.worker, "你是回声器", "只回复两个字：正常");
    std::cout << "  [ping] 耗时 " << (std::time(nullptr) - start) << "s，回复: "
              << (reply.empty() ? "<空>" : reply) << std::endl;
    EXPECT_FALSE(reply.empty());
}

// ===== S1：提名字软激活 YES → 入场发言 → lull 跟进 =====
TEST(EngagementSmoke, S1_SoftActivationYesAndFollowUp)
{
    if (smokeOut().empty())
        GTEST_SKIP() << "KLEIN_SMOKE_CONFIG is not configured";
    const SmokeWorld &world = SmokeWorld::get();
    Scenario s(77001, "s1");

    std::cout << "\n===== S1 软激活：游戏深渊话题，丁提名字求阵容 =====" << std::endl;
    s.feed(101, "白熊咖啡", "新深渊又打不过了 12层差两颗星");
    s.feed(102, "顶楼上分的", "这期上半那个圣骸兽太恶心了 一口一个脆皮");
    s.feed(103, "绝赞摸鱼中", "带个钟离吧 我钟离练度拉满了 走哪都站撸");
    s.feed(101, "白熊咖啡", "钟离我倒是没练 万叶行不行");
    s.feed(102, "顶楼上分的", "万叶聚怪是好 但是这期怪散 聚不太起来 我试了");
    s.feed(104, "深夜修仙", "Klein 你打这期深渊了吗 什么阵容好使");

    auto session = s.engagement->sessionOf(s.groupId);
    ASSERT_TRUE(session.has_value()) << "judge 应判定介入（被直接提问）";
    EXPECT_EQ(session->state, EngagementState::Active);
    std::cout << "  [会话] 已开启，话题行: " << session->topicLine << std::endl;
    ASSERT_FALSE(s.sender->texts.empty()) << "入场轮应发言";
    std::cout << "  [入场发言] " << s.sender->texts.front() << std::endl;

    // 后续讨论推进一拍：新消息攒够 + lull
    s.now += 5;
    s.feed(102, "顶楼上分的", "我全队80了还是刮痧 血月buff都吃满了");
    s.now += 3;
    s.feed(103, "绝赞摸鱼中", "词条太差了吧 你暴击率多少就敢打12层");
    s.now += 4;
    s.feed(101, "白熊咖啡", "我双爆50/150 感觉是手法问题 血月都躲不掉");
    const std::size_t workerCallsBefore = s.workerUsers.size();
    s.now += 70;
    s.engagement->pump(s.now);
        s.drain(); // 触发 lull 轮次（同步执行）
    std::cout << "  [跟进轮] 出站 " << s.sender->texts.size() << " 条，会话状态 "
              << (s.engagement->sessionOf(s.groupId)->state == EngagementState::Active
                      ? "存活"
                      : "结束:" + s.engagement->sessionOf(s.groupId)->endReason)
              << std::endl;
    EXPECT_GE(s.sender->texts.size(), 1U);
    EXPECT_LE(s.sender->texts.size(), 3U) << "跟进轮应克制（至多再插一嘴）";
    (void)world;
}

// ===== S2：提名字但聊的是数学家克莱因 → judge 应 NO =====
TEST(EngagementSmoke, S2_SoftActivationNoOnUnrelatedMention)
{
    if (smokeOut().empty())
        GTEST_SKIP() << "KLEIN_SMOKE_CONFIG is not configured";
    Scenario s(77002, "s2");

    std::cout << "\n===== S2 软激活 NO：聊数学家克莱因，与她无关 =====" << std::endl;
    s.feed(201, "数学系摸鱼人", "菲利克斯·克莱因这个人的瓶模型是真的优雅");
    s.now += 2;
    s.feed(202, "拓扑学废柴", "克莱因瓶四维嵌入我看不懂 但大受震撼");
    s.now += 2;
    s.feed(203, "半夜卷曲", "Klein 瓶到底有没有内外之分啊 求个直观解释");

    EXPECT_FALSE(s.engagement->sessionOf(s.groupId).has_value())
        << "judge 应判定不介入";
    EXPECT_TRUE(s.sender->texts.empty()) << "不应发言打扰";
    std::cout << "  [结果] 未开会话，未发言 ✓" << std::endl;
}

// ===== S3：@ 硬激活 → 真实 @ 回复路径 → 超长日志占位 → lull 跟进 =====
TEST(EngagementSmoke, S3_AtMentionRealReplyAndOversizedPlaceholder)
{
    if (smokeOut().empty())
        GTEST_SKIP() << "KLEIN_SMOKE_CONFIG is not configured";
    const SmokeWorld &world = SmokeWorld::get();
    Scenario s(77003, "s3");

    std::cout << "\n===== S3 @ 硬激活：编译报错求助（走真实 @ 回复路径） =====" << std::endl;
    s.context->onAtActivated(s.groupId, "@Klein 帮我看看这个链接错误");
    ASSERT_TRUE(s.engagement->sessionOf(s.groupId).has_value());
    std::cout << "  [会话] @ 硬激活已开启" << std::endl;

    // 真实 @ 回复路径（Message::handleMessage 同款装配）：assemble 注记 + 收敛契约
    const std::string question = "@Klein 帮我看看这个链接错误";
    InboundMessage at = s.chat(301, "键政勿扰", question, true);
    s.context->observe(at);
    std::string conversationText = question;
    std::string note = s.context->assemble(s.groupId, question);
    if (!note.empty())
        conversationText += "\n" + note;
    ChatReply reply = world.chat->reply(world.settings.bot.managerId, conversationText, true,
                                        std::nullopt,
                                        GroupContextService::groupConversationContract());
    ASSERT_FALSE(reply.text.empty());
    std::cout << "  [@ 回复] " << reply.text.substr(0, 80) << std::endl;
    s.sender->deliver(OutboundDelivery{GroupMessageTarget{std::to_string(s.groupId)},
                                       TextMessage{reply.text}});
    s.context->recordOutbound(s.groupId, TextMessage{reply.text});

    // 讨论推进：有人贴一段超长编译日志（>1000 token），窗口内应被占位
    s.now += 8;
    s.feed(302, "键政勿扰", "加了 -lsqlite3 还是不行 我是 cmake 项目 target_link_libraries 写了啊");
    s.now += 5;
    s.feed(304, "改bug改到天亮", "楼上的 find_package(SQLite3 REQUIRED) 和 ${SQLite3_LIBRARIES} 都要写 一个都不能少");
    s.now += 5;
    std::string log = "g++ -std=c++17 main.o db.o -o app\n";
    for (int index = 0; index < 90; ++index)
    {
        log += "/home/user/project/src/db.cpp:" + std::to_string(40 + index) +
               ": undefined reference to `sqlite3_open_v2'\n" +
               "/usr/bin/ld: /home/user/project/build/CMakeFiles/app.dir/main.o: in function "
               "`main': main.cpp:(.text+0x" +
               std::to_string(1000 + index * 7) + "): more undefined references\n";
    }
    s.feed(303, "自爆卡车", "我也遇到了 完整报错贴给你看：\n" + log);
    ASSERT_GT(EngagementService::estimateTokens(log), 1000U) << "构造的超长日志须超阈值";
    s.now += 6;
    s.feed(302, "键政勿扰", "楼上的建议我试了 find_package 也加了 还是炸 頭疼");

    const std::size_t workerCallsBefore = s.workerUsers.size();
    s.now += 70;
    s.engagement->pump(s.now);
        s.drain();
    ASSERT_GE(s.agentMaterials.size(), 1U);
    const std::string &material = s.agentMaterials.back();
    std::cout << "  [材料全文] " << material << std::endl;
    EXPECT_NE(material.find("[超长消息已省略]"), std::string::npos)
        << "超长日志应被占位（不灌进材料）";
    std::cout << "  [材料核验] 超长日志已占位，材料 " << material.size() << " 字符" << std::endl;
    std::cout << "  [跟进轮] 出站累计 " << s.sender->texts.size() << " 条" << std::endl;
}

// ===== S4：话题翻页 → 应离场或连续沉默收场 =====
TEST(EngagementSmoke, S4_TopicPageTurnLeadsToEnd)
{
    if (smokeOut().empty())
        GTEST_SKIP() << "KLEIN_SMOKE_CONFIG is not configured";
    Scenario s(77004, "s4");

    std::cout << "\n===== S4 话题翻页：聊电影切入聊搬家 =====" << std::endl;
    s.feed(401, "爆米花星人", "沙丘3定档了 大家去不去首映");
    s.now += 4;
    s.feed(402, "IMAX钉子户", "去 维伦纽瓦的IMAX必须冲 上部我刷了三遍");
    s.now += 4;
    s.feed(403, "文艺片难民", "Klein 你觉得沙丘2拍得怎么样 值得二刷吗");
    s.engagement->runJudge(s.groupId);
    ASSERT_TRUE(s.engagement->sessionOf(s.groupId).has_value()) << "被点名评价电影应介入";
    ASSERT_FALSE(s.sender->texts.empty());
    std::cout << "  [入场发言] " << s.sender->texts.back() << std::endl;

    // 群话题翻页：搬家
    // 群话题翻页：搬家（每拍 3 条真实讨论，喂足 lull 触发门槛 3 条）
    const char *moving[] = {
        "对了 求助 谁有跨城搬家推荐的物流公司 下周五要搬",
        "跨城搬家公司一般按立方收费 你东西多吗 有没有钢琴这种大件",
        "我上个月刚搬完 德邦和货拉拉对比了半天 最后走的货拉拉",
        "提前一周纸箱打包 易碎品塞衣服 搬运当天最好有两人在场盯",
        "冰箱洗衣机这种要提前断水断电 搬前24小时别再通电 不然压缩机容易坏",
        "新家宽带记得提前预约移机 不然到了没网能急死人 装维师傅要排期的",
        "搬家当天贵重物品自己随身别上车 现金证件首饰这些 老师傅都这么叮嘱",
        "跨城那种拼车搬家便宜但是慢 可能要等三四天才到 急用东西自己先带走",
        "签合同前问清楚有没有楼层费和大件费 不少公司低价揽客到了现场加钱",
    };
    const char *nicknames[] = {"爆米花星人", "IMAX钉子户", "文艺片难民", "刚搬完的"};
    auto feedPageTurn = [&, beat = 0]() mutable
    {
        for (int i = 0; i < 3; ++i)
        {
            const int seq = beat * 3 + i;
            s.now += 4;
            s.feed(401 + seq % 4, nicknames[seq % 4], moving[seq % 9]);
        }
        s.now += 65;
        s.engagement->pump(s.now);
        s.drain();
        return s.engagement->sessionOf(s.groupId)->state != EngagementState::Active;
    };
    bool ended = feedPageTurn();
    for (int beat = 0; beat < 3 && !ended; ++beat)
        ended = feedPageTurn();

    auto session = s.engagement->sessionOf(s.groupId);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Ended) << "话题翻页后应自然收场";
    std::cout << "  [收场] 原因: " << session->endReason << "，出站累计 "
              << s.sender->texts.size() << " 条" << std::endl;
}

// ===== S5：长文讨论触发滚动压缩 → 沉寂收场 → 保留 → 清旧建新 → 24h 清扫 =====
// S5 素材：真实装修群的长句讨论（经验正文 + 追问/补充两段式）
const char *longPosts[] = {
    "我家上个月刚收房 开始做装修了 建议大家水电阶段一定要自己到场盯 横平竖直看着好看其实后期安装打孔容易打到线管 我家厨房就打爆了一根热水管 幸好当场发现重排 拍照留底很重要 每一根管子走向都记下来",
    "半包和全包纠结了很久 最后选的半包 主材自己买 辅料施工队出 体验下来瓷砖洁具自己挑确实放心 但累是真的累 周末全泡在建材市场 大家如果上班忙还是考虑全包 贵一点买个省心",
    "防水真的不能省 卫生间墙面我刷到了1米8 淋浴区直接刷满 闭水试验做了48小时去楼下看了两次 一点渗漏没有 邻居也放心 之前看群里有人说只刷30公分结果楼下天花板遭殃 赔的钱比省的多多了",
    "全屋定制水太深了 朋友们 板材环保等级认准ENF级 E0和E1都是老的提法 五金件问清楚是不是百隆海蒂诗贴牌 报价单要让他们写清楚投影面积还是展开面积 差别能有一万多 我就被坑过",
    "灯光设计推荐无主灯 磁吸轨道加筒灯射灯组合 客厅亮度完全够 而且氛围感直接拉满 色温统一3500K暖白 千万别混色温 血泪教训 我家第一版混了3000和4000K 打开像医院走廊 最后全部换了",
    "瓷砖美缝自己做还是找人做 自己做真的便宜 三百块材料费搞定 但是腰会断 打了三组美缝剂第二天胳膊抬不起来 预算够还是找人做 一天完工效果还均匀 手残党别逞强",
    "中央空调和风管机纠结了很久 最后三个房间都装的风管机 一拖一的独立控制 坏了不影响其他房间 而且比中央空调便宜不少 电费也省 就是外机位要多占几个 小户型慎重",
    "乳胶漆别只看牌子 看环保报告和耐擦洗次数 五合一和全效差别不大 基础款够用 刷之前一定要打底漆 不然墙面泛碱 顶面建议用白色耐擦洗的 别学网上的什么奶油色 时间长了看着腻",
    "厨房动线真的是灵魂 洗切炒一字排开 台面高度按主厨身高定 我家做的85公分 高灶台矮水槽 符合人体工学 做饭一小时腰不酸 橱柜抽屉比层柜好用 拿东西不用蹲 下次装修全上抽屉",
    "封窗一定要用断桥铝 型材厚度问清楚1.4还是1.8 玻璃要中空双层隔音隔热效果完全不同 我家临街 换了断桥铝之后噪音直接小了一半 高层风大 平开窗五金件别省钱 开合手感差很多",
    "水电改造完记得要水电走向图 装修公司都会给 但是要检查是不是和实际走线一致 我家图上画的和实际差了半米 后期装镜子差点打爆线管 自己再拍一遍视频留底最保险",
    "装修完一定要通风三个月起步 除甲醛最有效的还是开窗通风 绿萝活性炭都是心理安慰 买两个工业风扇对着窗户吹 加速空气流动 天气好多开窗 淡季入住比啥除醛产品都管用",
    "预算一定要留两成余量 我见过的几乎没有不超支的 增项大户是水电和全屋定制 签合同前把增项上限写进去 超过部分装修公司承担 谈判的时候脸皮要厚 后期省的是真金白银",
    "卫浴室下水和排水坡度施工时一定盯 瓦工图省事坡度不够 洗完澡积水要拿扫把扫 我家返工了一次 地漏用T型和U型看使用频率 常用水多的用T型深水封防臭效果好 不容易干",
    "半包真心建议找个第三方监理 水电验收和瓦工验收各来一次 一次四五百 比装修公司自检靠谱 他收你钱只对你负责 拉着监理一起验收 工人明显认真很多",
    "窗帘盒要在吊顶时就做 留轨道槽 单层帘15公分双层20公分 电动窗帘记得预留电源位 不然后期走明线丑哭 深度不够轨道会卡帘子",
    "淋浴房隔断选极窄边框的玻璃门 五金奠基吊轮一定买好的 每天开合几十次 便宜的一半年就涩了 防爆膜贴里面 外层也可以贴",
    "开关插座点位宁多勿少 床头双控 电视墙五孔带开关 马桶边留智能盖板电源 洗衣机阳台留位 弱电箱预留插座给路由器 竣工图全标注",
    "垃圾清运和保洁别忘在合同里 装修完的碎砖水泥袋物业不让扔电梯 清运费按车算 一车几百 找装修公司代叫省心 开荒保洁按平米",
    "冬季施工水泥砂浆要防冻 低于五度别贴砖 不然开春空鼓 木材进场先适应环境两天再安装 不然热胀冷缩接缝炸缝",
    "卫生间沉箱回填别用建渣 一次回填打九格上钢筋网再找平 顶面做二次排水 我家第一次被偷工改料用建渣 两年后渗到楼下返工",
    "地暖盘管间距20公分一步到位 别信16公分的升温快论调 回水管做坡度 分水器路数按房间配 实铺面积留着别铺满衣柜下方",
    "壁挂炉选冷凝式的 热效率高一个档次 暖气片别包进柜子里 散热效率腰斩 温控阀每个房间独立控温 燃气费省三成",
    "前置过滤器装水表后端 反冲洗型 末端净水器看通量 800G起步 厨房下预留电源和排水口 不然后期软水机没地方装",
    "洗碗机选13套嵌入式 能放炒锅才实用 烘干选自动开门或热风 余温烘干的碗第二天有水渍 台面高度按洗碗机顶盖齐平",
    "洗烘套装叠放要预埋支架 墙排水的烘干机接排水管别用储水盒 热泵烘干伤衣少 毛絮过滤器两层的 告别晾衣架才是真香",
    "阳台想好功能再封 洗晒区地面做坡度和地漏 电动晾衣架预留顶面电源 储物柜做到顶 猫笼和清洁工具全塞得下",
    "玄关做悬浮柜加灯带 下班进门有仪式感 底下留空15公分放常穿鞋 中间留空台面放钥匙 全身镜挂侧面 出门最后检查",
    "临街房子隔音窗是刚需 夹胶玻璃比中空隔音好 中低频的轮胎声靠夹胶层 吸音帘只是安慰剂 卧室背景墙做软包实测降了三分贝",
    "吊顶龙骨用轻钢的 木龙骨易变形开裂 转角处七字切割 整板套割 主灯位要加固处理 吊杆间距别超过八十",
    "瓷砖阳角用海棠角倒角 别用塑料扣条 四十五度碰角最考验瓦工手艺 美缝剂填进去立体感强",
    "石膏板吊顶选九点五毫米标准厚度 湿区用防水板 自攻钉间距二十公分 钉眼做防锈 再贴网格布",
};
const char *followUps[] = {
    "对了水电验收要做打压测试 水路0.8兆帕半小时不掉压才合格 电路用摇表测绝缘 这半小时省的是后面几万块的砸墙返工",
    "主材购买顺序有讲究 瓷砖洁具要先定 水电定位跟尺寸走 中央空调要在水电前进场 工期要倒着排才不乱",
    "防水做完贴砖前让瓦工拉毛 不然后期空鼓脱落 闭水试验最好挑晚上去楼下看天花板 开灯才看得清水渍",
    "板材检测报告要商家盖章那份 样品和批量货可能不是一个批次 有条件留一块样板送检 图个安心",
    "无主灯的变压器要藏好 每个磁吸灯位留检修口 不然换驱动要拆吊顶 3500K是我踩三次坑换来的",
    "美缝剂选环氧的 别用真瓷胶 一年变色 深色砖配哑光美缝 施工前缝隙要清到2毫米深",
    "风管机检修口别让木工封死 留45公分 滤网一年洗两次 内机高度要和吊顶图纸对上",
    "乳胶漆一底两面是底线 底漆省了面漆全是细孔 雨季施工开抽湿 不然漆膜起泡 窗边尤其注意",
    "厨房插座宁多勿少 台面上方留四五个 嵌入式电器独立回路 冰箱单独一路 出远门断电不影响别处",
    "断桥铝隔热条看宽度 20毫米以上才算真断桥 要尼龙PA66的别信PVC 玻璃认准3C钢化标",
    "水电走向图存电子版云盘备份 纸质会丢 后期装镜子装挂钩全靠这张图保命",
    "除甲醛最忌讳闷放 关窗闷一天再猛开窗 比一直开窗有效 高温高湿利于释放 这是物理",
    "增项谈判带着预算表去 管理费辛苦费一次谈死写进合同 后期不加钱 脸皮厚点",
    "验收带空鼓锤和相位检测仪 空鼓当场敲出来让返 相位不对烧电器 整改完再签字",
    "监理验收那天全程跟着 看他带不带打压泵和摇表 没带装备的就是走过场 直接换人",
    "电动窗帘轨道提前买好给吊顶师傅 装完再补要加钱还费事 双层帘轨道间距留5公分不互相打架",
    "吊轮买轴承的 推一百次试试顺不顺滑 防爆膜五十九一平 别听商家吹自防爆",
    "点位按家电清单倒推 扫地机器人基站 智能马桶 烘箱洗碗机全列出来 上下水一起定",
    "清运费谈包干 按车算容易被塞水分 开荒保洁玻璃内外都擦 包含厨卫除垢",
    "加抗冻剂只是辅助 温度才是硬道理 木工板材进屋别靠暖气片太近 开裂变形都是这么来的",
    "回填完做一次闭水复验 别嫌麻烦 二次排水口对着坡度最低点 我家现在淋浴水自己走",
    "保温板选挤塑板 反射膜选带刻度的 卡子用免打孔的 盘完管拍照存档 装挂钩心里有底",
    "烟气别排进公共烟道止逆阀必装 冷凝水预留排水口 靠外墙的烟孔打斜孔防倒灌",
    "软化水盐附近超市就有卖 别囤太多 一袋半年 软水不进厨房饮用水口 单独走一路",
    "洗碗机盐和亮碟剂首次要加满 软水档位按当地水质调 不然玻璃器皿发白 还以为是没洗干净",
    "烘干完的床品直接铺 除螨效果比晒太阳强 雨季南方人的救生船 空气洗功能去火锅味一绝",
    "封完窗地面先做保护 不然后期瓦工把玻璃刮花 尾款压到保洁完再结",
    "悬浮柜承重靠拉爆螺栓别打在龙骨上 灯带选低压的24V 变压器藏柜内 检修不拆柜",
    "隔音窗装完用手电贴缝照一圈 漏光就是漏音 压条胶条三年一换属正常损耗",
    "转角七字板就是一整块板切V槽折过去 不断开 拼缝留在受力小的平面上 开裂率直线下滑",
    "海棠角缝隙均匀能看到砖坯色是正常的 扣条反而是以后翘边的重灾区",
    "钉眼防锈漆点一遍就够了 贴布是防热胀冷缩的开裂 双保险不冲突",
};

TEST(EngagementSmoke, S5_CompressionRetentionAndReplacement)
{
    if (smokeOut().empty())
        GTEST_SKIP() << "KLEIN_SMOKE_CONFIG is not configured";
    Scenario s(77005, "s5");

    std::cout << "\n===== S5 长文装修讨论（滚动压缩）+ 保留/清旧建新生命周期 =====" << std::endl;
    // 每条 = 经验分享 + 追问补充（真实装修群的长句节奏）
    std::vector<std::string> posts;
    const std::size_t postCount = std::min(sizeof(longPosts) / sizeof(longPosts[0]),
                                           sizeof(followUps) / sizeof(followUps[0]));
    for (std::size_t index = 0; index < postCount; ++index)
        posts.push_back(std::string(longPosts[index]) + " " + followUps[index] + " " +
                        followUps[(index + 7) % postCount]);

    // 第一波：先积累大量群内容（此刻无会话，纯观察入库）
    for (std::size_t index = 0; index < 20; ++index)
    {
        s.feed(501 + index % 3, index % 2 == 0 ? "新房小白鼠" : "装修老油条", posts[index]);
        s.now += 25;
    }

    // 被点名提问：judge → 入场轮（材料 ~21 条 ≈ 2300 token，未超预算）
    s.feed(504, "预算快爆了", "Klein 你是行家 你说全屋定制和木工打柜子到底怎么选");
    ASSERT_TRUE(s.engagement->sessionOf(s.groupId).has_value()) << "被点名问装修应介入";
    ASSERT_FALSE(s.sender->texts.empty()) << "被直接提问应发言";
    std::cout << "  [入场发言] " << s.sender->texts.back().substr(0, 60) << "…" << std::endl;

    // 第二波：讨论继续，材料涨过 3000 token；点名她追问 → 必须回应的一轮
    for (std::size_t index = 20; index < 26; ++index)
    {
        s.feed(501 + index % 3, index % 2 == 0 ? "新房小白鼠" : "装修老油条", posts[index]);
        s.now += 25;
    }
    const std::size_t workerCallsBefore = s.workerUsers.size();
    s.feed(504, "预算快爆了", "Klein 你刚说的封边工艺 具体怎么分辨好坏");
    ASSERT_GT(s.workerUsers.size(), workerCallsBefore) << "满窗超预算应触发杂务模型压缩";

    auto session = s.engagement->sessionOf(s.groupId);
    ASSERT_TRUE(session.has_value());
    EXPECT_FALSE(session->digest.empty()) << "滚动摘要应已生成";
    std::cout << "  [滚动压缩] 摘要 " << session->digest.size() << " 字符，水位 ts="
              << session->compressedUpToTs << std::endl;
    ASSERT_FALSE(s.agentMaterials.empty());
    EXPECT_NE(s.agentMaterials.back().find("更早讨论的摘要"), std::string::npos)
        << "压缩后材料应携带摘要块";
    std::cout << "  [追问轮] 出站累计 " << s.sender->texts.size() << " 条" << std::endl;

    // 沉寂收场 → 保留
    s.now += 11 * 60;
    s.engagement->pump(s.now);
    s.drain();
    session = s.engagement->sessionOf(s.groupId);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Ended);
    std::cout << "  [收场] " << session->endReason << "，进入 24h 保留期" << std::endl;

    // 保留期内新话题 @：清旧建新
    s.now += 2 * 60 * 60;
    s.context->onAtActivated(s.groupId, "下午新话题：阳台要不要封");
    session = s.engagement->sessionOf(s.groupId);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Active);
    EXPECT_EQ(session->topicLine, "下午新话题：阳台要不要封");
    std::cout << "  [清旧建新] 旧话题已让位，新会话存活" << std::endl;

    // 新会话 30 分钟无跟进 → 时长硬后盖
    s.now += 31 * 60;
    s.engagement->pump(s.now);
    s.drain();
    session = s.engagement->sessionOf(s.groupId);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->state, EngagementState::Ended);
    std::cout << "  [硬后盖] " << session->endReason << std::endl;

    // 保留期满：清扫
    s.now += 25 * 60 * 60;
    s.engagement->pump(s.now);
    s.drain();
    EXPECT_FALSE(s.engagement->sessionOf(s.groupId).has_value()) << "保留期满应被清扫";
    std::cout << "  [清扫] 保留期满，会话引用已清理 ✓" << std::endl;

    std::cout << "  [DB] group_messages 共 " << s.store->snapshot(s.groupId).size()
              << " 条（她的发言在库，随 24h TTL 自然淘汰）" << std::endl;
}

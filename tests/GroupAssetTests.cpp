#include <gtest/gtest.h>

#include "Asset/ImageAssetStore.h"
#include "Perception/GroupContextService.h"
#include "Perception/GroupContextStore.h"
#include "Perception/GroupListService.h"
#include "Port/InboundMessage.h"
#include "Tool/ImageToolHelpers.h"
#include "Tool/SendImageTool.h"
#include "Tool/ToolContext.h"

#include <cstdlib>
#include <filesystem>
#include <regex>
#include <string>

namespace
{
class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-groupasset-XXXXXX";
        pattern.push_back('\0');
        char *created = mkdtemp(pattern.data());
        if (created != nullptr)
            directory = created;
    }
    ~TemporaryDirectory()
    {
        if (!directory.empty())
            std::filesystem::remove_all(directory);
    }
    const std::string &path() const { return directory; }

private:
    std::string directory;
};

// 无网络的"实现端"：GroupListService 需要一个 API 通道
class NoopApiChannel final : public OneBotApiChannel
{
public:
    OneBotApiResult call(const std::string &, nlohmann::json, std::chrono::milliseconds) override
    {
        OneBotApiResult result;
        result.networkError = true;
        return result;
    }
};
} // namespace

// ---------- 第二实例：独立表、独立前缀、同一数据库文件 ----------

TEST(GroupAssetStoreTest, SecondInstanceUsesOwnTableAndPrefixInSameDatabase)
{
    TemporaryDirectory directory;
    const std::string dbPath = directory.path() + "/assets.db";
    ImageAssetStore personal(dbPath, directory.path() + "/images");
    ImageAssetStore group(dbPath, directory.path() + "/images/group", "group_image_assets", "gimg_");

    const auto mine = personal.saveBase64(10, "aGVsbG8=", "inbound", "", 1);
    const auto ours = group.saveBase64(88, "d29ybGQ=", "inbound", "", 2);
    ASSERT_TRUE(mine.has_value());
    ASSERT_TRUE(ours.has_value());
    EXPECT_TRUE(std::regex_match(mine->asset_id, std::regex(R"(^img_[0-9a-f]{32}$)")));
    EXPECT_TRUE(std::regex_match(ours->asset_id, std::regex(R"(^gimg_[0-9a-f]{32}$)")));
    EXPECT_EQ(group.tableName(), "group_image_assets");
    EXPECT_NE(ours->local_path.find("/images/group/"), std::string::npos);

    // 互不可见：个人库查不到群资产，群库查不到个人资产；owner 维度也隔离
    EXPECT_FALSE(personal.find(88, ours->asset_id).has_value());
    EXPECT_FALSE(group.find(10, mine->asset_id).has_value());
    EXPECT_FALSE(group.find(99, ours->asset_id).has_value());
    EXPECT_TRUE(group.find(88, ours->asset_id).has_value());
    EXPECT_EQ(group.readBase64(*ours), "d29ybGQ=");

    // 按来源消息 / 本地路径反查
    EXPECT_EQ(group.findBySourceMessage(88, 2)->asset_id, ours->asset_id);
    EXPECT_FALSE(group.findBySourceMessage(88, 0).has_value());
    EXPECT_FALSE(group.findBySourceMessage(89, 2).has_value());
    EXPECT_EQ(group.findByLocalPath(88, ours->local_path)->asset_id, ours->asset_id);
    EXPECT_FALSE(group.findByLocalPath(88, "/nowhere.png").has_value());

    // 个人库的清理不碰群库
    personal.clearUser(10);
    EXPECT_FALSE(personal.find(10, mine->asset_id).has_value());
    EXPECT_TRUE(group.find(88, ours->asset_id).has_value());
    EXPECT_TRUE(std::filesystem::exists(ours->local_path));
}

TEST(GroupAssetStoreTest, PruneAppliesTtlAndPerOwnerCapAndDeletesFiles)
{
    TemporaryDirectory directory;
    ImageAssetStore group(directory.path() + "/assets.db", directory.path() + "/g",
                          "group_image_assets", "gimg_");
    // created_at 由 saveBase64 取当前时间；用 TTL 边界构造"过期"
    const auto a = group.saveBase64(88, "YQ==", "inbound", "", 1);
    const auto b = group.saveBase64(88, "Yg==", "inbound", "", 2);
    const auto c = group.saveBase64(88, "Yw==", "generated", "p", 3);
    const auto other = group.saveBase64(99, "ZA==", "inbound", "", 4);
    ASSERT_TRUE(a && b && c && other);
    const int64_t now = c->created_at;

    // 未过期且未超帽：不删
    EXPECT_EQ(group.prune(now, 24 * 3600, 300), 0U);
    // 每群只留 2 张：88 群最旧的 a 被淘汰，99 群不受影响
    EXPECT_EQ(group.prune(now, 24 * 3600, 2), 1U);
    EXPECT_FALSE(group.find(88, a->asset_id).has_value());
    EXPECT_FALSE(std::filesystem::exists(a->local_path));
    EXPECT_TRUE(group.find(88, b->asset_id).has_value());
    EXPECT_TRUE(group.find(99, other->asset_id).has_value());
    // TTL：把"现在"推到 24h 之后一秒，全部过期
    EXPECT_EQ(group.prune(now + 24 * 3600 + 1, 24 * 3600, 300), 3U);
    EXPECT_FALSE(group.find(88, b->asset_id).has_value());
    EXPECT_FALSE(group.find(99, other->asset_id).has_value());
    EXPECT_FALSE(std::filesystem::exists(other->local_path));
}

// ---------- 工具解析顺序：群优先、个人兜底、私聊不变 ----------

TEST(ImageToolScopeTest, SharedScopeResolvesGroupFirstThenPersonalAndPrivateUnchanged)
{
    TemporaryDirectory directory;
    const std::string dbPath = directory.path() + "/assets.db";
    ImageAssetStore personal(dbPath, directory.path() + "/images");
    ImageAssetStore group(dbPath, directory.path() + "/images/group", "group_image_assets", "gimg_");
    const auto personalGen = personal.saveBase64(10, "cA==", "generated", "", 1);
    const auto personalIn = personal.saveBase64(10, "cQ==", "inbound", "", 2);
    const auto groupIn = group.saveBase64(88, "cg==", "inbound", "", 3);
    ASSERT_TRUE(personalGen && personalIn && groupIn);

    ToolContext inGroup{10, 0, "", 88};
    ToolContext inPrivate{10, 0, "", 0};
    ToolContext strangerInGroup{20, 0, "", 88};

    // 群里：按来源 inbound → 群库命中（不是本人的个人库那张）
    EXPECT_EQ(resolveImageAsset(personal, &group, inGroup, {{"source", "inbound"}})->asset_id,
              groupIn->asset_id);
    // 群里：asset_id 指向本人的个人资产 → 群库查不到，回退个人库命中
    EXPECT_EQ(resolveImageAsset(personal, &group, inGroup, {{"asset_id", personalIn->asset_id}})->asset_id,
              personalIn->asset_id);
    // 群里：别人引用你的个人资产 id → 群库没有、他的个人库也没有这个 id → 不泄漏你的那张，
    // 而是兜底到群库最近一张，并在 note 里说明改用了哪张（2026-10-29 真机缺陷修复）
    {
        std::string note;
        const auto asset = resolveImageAsset(personal, &group, strangerInGroup,
                                             {{"asset_id", personalIn->asset_id}}, &note);
        ASSERT_TRUE(asset.has_value());
        EXPECT_EQ(asset->asset_id, groupIn->asset_id);
        EXPECT_NE(note.find(personalIn->asset_id), std::string::npos);
        EXPECT_NE(note.find(groupIn->asset_id), std::string::npos);
    }
    // 群里无参数：不再默认"最近一张生成图"，而是群库里最近一张（任意来源）
    {
        std::string note;
        const auto asset = resolveImageAsset(personal, &group, inGroup, nlohmann::json::object(), &note);
        ASSERT_TRUE(asset.has_value());
        EXPECT_EQ(asset->asset_id, groupIn->asset_id);
        EXPECT_TRUE(note.empty()); // 没给 id 就不算"改用"，不打扰模型
    }
    // 群里给了抄错的 id：群库最近一张兜底 + note
    {
        std::string note;
        const auto asset = resolveImageAsset(personal, &group, inGroup, {{"asset_id", "gimg_no_such"}}, &note);
        ASSERT_TRUE(asset.has_value());
        EXPECT_EQ(asset->asset_id, groupIn->asset_id);
        EXPECT_NE(note.find("gimg_no_such"), std::string::npos);
    }
    // 群里 source=generated 但群库没有生成图 → 个人库本人的生成图（第 2 级在个人库命中）
    EXPECT_EQ(resolveImageAsset(personal, &group, inGroup, {{"source", "generated"}})->asset_id,
              personalGen->asset_id);
    // 私聊：只看个人库，群库里的图不可见；行为与旧签名一致
    EXPECT_EQ(resolveImageAsset(personal, &group, inPrivate, {{"source", "inbound"}})->asset_id,
              personalIn->asset_id);
    EXPECT_EQ(resolveImageAsset(personal, 10, {{"source", "inbound"}})->asset_id, personalIn->asset_id);
    // 私聊引用群库 id：群库不可见，个人库没有这个 id → 兜底到本人最近一张（任意来源）
    {
        std::string note;
        const auto asset = resolveImageAsset(personal, &group, inPrivate, {{"asset_id", groupIn->asset_id}}, &note);
        ASSERT_TRUE(asset.has_value());
        EXPECT_EQ(asset->asset_id, personalIn->asset_id);
        EXPECT_FALSE(note.empty());
    }
    // 私聊无参数：最近一张（任意来源），不再偏向生成图
    EXPECT_EQ(resolveImageAsset(personal, &group, inPrivate, nlohmann::json::object())->asset_id,
              personalIn->asset_id);
    // 什么都没有的人：三级都空 → 查不到
    ToolContext nobody{30, 0, "", 0};
    EXPECT_FALSE(resolveImageAsset(personal, &group, nobody, nlohmann::json::object()).has_value());
    // 没有群库指针时群聊退化为个人库
    EXPECT_EQ(resolveImageAsset(personal, nullptr, inGroup, {{"source", "inbound"}})->asset_id,
              personalIn->asset_id);
}

TEST(ImageToolScopeTest, SendImageToolSendsGroupAssetInGroupScope)
{
    TemporaryDirectory directory;
    const std::string dbPath = directory.path() + "/assets.db";
    ImageAssetStore personal(dbPath, directory.path() + "/images");
    ImageAssetStore group(dbPath, directory.path() + "/images/group", "group_image_assets", "gimg_");
    const auto personalGen = personal.saveBase64(10, "cA==", "generated", "", 1);
    const auto groupGen = group.saveBase64(88, "cg==", "generated", "p", 3);
    ASSERT_TRUE(personalGen && groupGen);
    SendImageTool tool(personal, &group);

    const ToolResult inGroup = tool.execute("{}", ToolContext{10, 0, "", 88});
    ASSERT_EQ(inGroup.outbound_messages.size(), 1U);
    EXPECT_EQ(std::get<ImageMessage>(inGroup.outbound_messages[0]).data, groupGen->local_path);
    const ToolResult inPrivate = tool.execute("{}", ToolContext{10, 0, "", 0});
    ASSERT_EQ(inPrivate.outbound_messages.size(), 1U);
    EXPECT_EQ(std::get<ImageMessage>(inPrivate.outbound_messages[0]).data, personalGen->local_path);
}

// ---------- 观察路径：图片进群库，占位符带 asset_id ----------

TEST(GroupContextImageTest, ObserveImportsImageIntoSharedStoreAndWritesPlaceholder)
{
    TemporaryDirectory directory;
    const std::string dbPath = directory.path() + "/ctx.db";
    GroupContextStore store(dbPath);
    ImageAssetStore group(dbPath, directory.path() + "/g", "group_image_assets", "gimg_");
    NoopApiChannel api;
    GroupListService state(directory.path() + "/state.db", api);
    state.setFeatureEnabled(true);
    state.setMonitored(88, true);
    BotIdentity bot;
    bot.id = 10086;
    bot.name = "Klein";
    GroupContextService service(&state, bot, &store, nullptr);
    service.setSharedAssets(&group);
    // 免网络：导入 seam 直接把"下载结果"存进群库
    service.setImageImporter([&group](std::uint64_t groupId, const std::string &url, std::int64_t messageId) {
        return url == "http://img/ok" ? group.saveBase64(groupId, "aGk=", "inbound", "", messageId)
                                      : std::nullopt;
    });

    InboundMessage withImage;
    withImage.post_type = "message";
    withImage.message_type = "group";
    withImage.group_id = 88;
    withImage.user_id = 1;
    withImage.nickname = "A";
    withImage.message_id = 5001;
    withImage.message_data_url = "http://img/ok";
    withImage.plain_text = "看这个";
    withImage.message_timestamp = 1000;
    service.observe(withImage);

    InboundMessage imageOnly = withImage;
    imageOnly.message_id = 5002;
    imageOnly.plain_text.clear();
    service.observe(imageOnly);

    InboundMessage failed = withImage;
    failed.message_id = 5003;
    failed.message_data_url = "http://img/broken";
    failed.plain_text.clear();
    service.observe(failed);

    const auto records = store.snapshot(88);
    ASSERT_EQ(records.size(), 3U);
    const auto first = group.findBySourceMessage(88, 5001);
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(records[0].text, "看这个 [image asset_id=" + first->asset_id + " source=inbound]");
    const auto second = group.findBySourceMessage(88, 5002);
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(records[1].text, "[image asset_id=" + second->asset_id + " source=inbound]");
    EXPECT_EQ(records[2].text, "[图片]") << "下载失败退回旧占位符";

    // 她生成的图经 recordOutbound 入库时按路径反查出占位符
    const auto generated = group.saveBase64(88, "Zw==", "generated", "一只猫", 0);
    ASSERT_TRUE(generated.has_value());
    service.recordOutbound(88, ImageMessage{ImageMessage::Source::LocalPath, generated->local_path});
    service.recordOutbound(88, ImageMessage{ImageMessage::Source::Url, "https://x/y.png"});
    const auto after = store.snapshot(88);
    ASSERT_EQ(after.size(), 5U);
    EXPECT_EQ(after[3].text, "[image asset_id=" + generated->asset_id + " source=generated]");
    EXPECT_TRUE(after[3].isSelf);
    EXPECT_EQ(after[4].text, "[图片]");
}

// R1.1 零成本表情：贴纸图占位符带 sticker 标记，mface/face 摘要拼成 [表情 …]
TEST(GroupContextImageTest, ObserveMarksStickerAndAppendsFaceSummaries)
{
    TemporaryDirectory directory;
    const std::string dbPath = directory.path() + "/ctx.db";
    GroupContextStore store(dbPath);
    ImageAssetStore group(dbPath, directory.path() + "/g", "group_image_assets", "gimg_");
    NoopApiChannel api;
    GroupListService state(directory.path() + "/state.db", api);
    state.setFeatureEnabled(true);
    state.setMonitored(88, true);
    BotIdentity bot;
    bot.id = 10086;
    bot.name = "Klein";
    GroupContextService service(&state, bot, &store, nullptr);
    service.setSharedAssets(&group);
    service.setImageImporter([&group](std::uint64_t groupId, const std::string &, std::int64_t messageId) {
        return group.saveBase64(groupId, "aGk=", "inbound", "", messageId);
    });

    InboundMessage sticker;
    sticker.post_type = "message";
    sticker.message_type = "group";
    sticker.group_id = 88;
    sticker.user_id = 1;
    sticker.nickname = "A";
    sticker.message_id = 7001;
    sticker.message_timestamp = 1000;
    sticker.plain_text = "哈哈";
    sticker.message_data_url = "http://img/sticker";
    sticker.image_is_sticker = true;
    sticker.image_summary = "[动画表情]";
    sticker.face_summaries = {"[狗头]", "face:14"};
    service.observe(sticker);

    // 只有表情段、没有文字没有图片：不再退回 "[图片]"
    InboundMessage facesOnly;
    facesOnly.post_type = "message";
    facesOnly.message_type = "group";
    facesOnly.group_id = 88;
    facesOnly.user_id = 2;
    facesOnly.nickname = "B";
    facesOnly.message_id = 7002;
    facesOnly.message_timestamp = 1001;
    facesOnly.face_summaries = {"[捂脸]"};
    service.observe(facesOnly);

    // 普通图片不带 sticker
    InboundMessage photo = sticker;
    photo.message_id = 7003;
    photo.plain_text.clear();
    photo.image_is_sticker = false;
    photo.image_summary.clear();
    photo.face_summaries.clear();
    service.observe(photo);

    const auto records = store.snapshot(88);
    ASSERT_EQ(records.size(), 3U);
    const auto stickerAsset = group.findBySourceMessage(88, 7001);
    ASSERT_TRUE(stickerAsset.has_value());
    EXPECT_EQ(records[0].text, "哈哈 [表情 狗头] [表情 face:14] [image asset_id=" +
                                   stickerAsset->asset_id + " source=inbound sticker]");
    EXPECT_EQ(records[1].text, "[表情 捂脸]");
    const auto photoAsset = group.findBySourceMessage(88, 7003);
    ASSERT_TRUE(photoAsset.has_value());
    EXPECT_EQ(records[2].text, "[image asset_id=" + photoAsset->asset_id + " source=inbound]");
}

#ifndef GROUP_CONTEXT_SERVICE_H
#define GROUP_CONTEXT_SERVICE_H

/*
 * 群上下文服务（T7b）：白名单群的内容观察 + 被 @ 时的上下文装配。
 * 群上下文是"按需装配的派生上下文"——每次被 @ 从内容库现场选择、
 * 格式化（小材料直灌 / 大材料经摘要器），附在用户消息尾部，本轮
 * 结束即弃，不落 Person 不进长期记忆。
 *
 * 选择（人类联想的机械版）：触发消息作查询（复用长期记忆的
 * buildRecallQueryPlan/scoreRecallText），对群消息逐条打分 ×新近
 * 衰减取 top-K；必选集=水位线（最近5条）+信箱（@她/提她名字）+
 * 她自己的近期出站——并行话题与"B 的反驳"由此天然入选/落选，
 * 不做任何预分割。
 *
 * 话题跟进（意志层 v2）委托 EngagementService：观察入库喂会话、
 * @ 硬激活转发、她的出站计活动。本类只保留内容层职责。
 */
#include "GroupContextStore.h"
#include "GroupListService.h"
#include "../Application/BotIdentity.h"
#include "../Asset/ImageAsset.h"
#include "../Port/InboundMessage.h"
#include "../Port/OutboundMessage.h"
#include "../Port/MessageSenderPort.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

class EngagementService;
class GlanceService;
class ImageAssetStore;

class GroupContextService
{
public:
    // summarizer：单次独立调用 seam（组合根适配 ChatService::buildOnce，
    // 测试注入 fake）；空/失败时装配退回截断原文
    using Summarizer = std::function<std::string(const std::string &systemPrompt,
                                                  const std::string &userPrompt)>;

    // state：观察状态（总开关 + 监控群集）唯一真值来源，逐步查询以支持即时生效
    GroupContextService(GroupListService *state, BotIdentity bot,
                        GroupContextStore *store, MessageSenderPort *sender = nullptr);

    void setSummarizer(Summarizer summarizer) { this->summarizer = std::move(summarizer); }
    // 话题会话服务（组合根注入；观察入库/@ 激活/出站活动都喂给它）
    void setEngagement(EngagementService *engagement) { this->engagement = engagement; }
    // 群资产库（ADR-019）：入站图片在此入库，缓冲文本写 [image asset_id=…] 占位符而非
    // "[图片]"；出站生成图按本地路径反查占位符。可空 = 维持旧占位符行为
    void setSharedAssets(ImageAssetStore *assets) { this->sharedAssets = assets; }
    // 图片导入 seam（默认走 sharedAssets->importFromUrl；测试注入 fake 免网络）
    using ImageImporter = std::function<std::optional<ImageAsset>(std::uint64_t groupId,
                                                                   const std::string &url,
                                                                   std::int64_t messageId)>;
    void setImageImporter(ImageImporter importer) { this->imageImporter = std::move(importer); }
    // 群聊瞥视（R1.1）：入站图片入库并拿到 seq 后交给它异步写回描述；可空 = 不瞥视。
    // 她自己的出站图不瞥视
    void setGlance(GlanceService *glance) { this->glance = glance; }

    // 白名单群消息入库（@ 与非 @ 都算群内容）；worker 线程调用
    void observe(const InboundMessage &message);

    // Klein 自己的群聊出站入库（文本/占位符），Message::dispatch 调用。
    // correlationId：该投递的回执关联键（R0.2），回执到达后用它回填真实 message_id
    void recordOutbound(std::uint64_t groupId, const OutboundMessage &outbound,
                        const std::string &correlationId = {});

    // 被 @ 时的上下文装配：返回附在用户消息尾部的注记块；无相关内容返回空串
    std::string assemble(std::uint64_t groupId, const std::string &triggerText) const;

    // @ 硬激活转发（Message 处理群 @ 时调用）：回答走既有回复路径，
    // 会话的开启/合并由 EngagementService 决定
    void onAtActivated(std::uint64_t groupId, const std::string &triggerText);

    // [不回应] 标记检测（群聊收敛契约的抑制判据，Message 层调用）
    static bool isSuppressed(const std::string &replyText);

    // 群聊收敛契约（静态文本，作为 situationNote 进 system——行为约束归
    // system 的 D13 分工；常量保证管理员前缀缓存逐字稳定）
    static const char *groupConversationContract();

private:
    struct Scored
    {
        const GroupMessageRecord *record = nullptr;
        double score = 0.0;
    };

    // 该群此刻是否在观察（总开关 + 监控集，实时查询以便即时生效）
    bool observingGroup(std::uint64_t groupId) const;
    std::string formatRecords(const std::vector<GroupMessageRecord> &records,
                              std::int64_t watermarkTs) const;
    std::string digestRecords(const std::vector<GroupMessageRecord> &records) const;

    GroupListService *const state;
    const BotIdentity bot;
    GroupContextStore *const store;
    MessageSenderPort *const sender;
    Summarizer summarizer;
    EngagementService *engagement = nullptr;
    ImageAssetStore *sharedAssets = nullptr;
    ImageImporter imageImporter;
    GlanceService *glance = nullptr;
};

#endif // GROUP_CONTEXT_SERVICE_H

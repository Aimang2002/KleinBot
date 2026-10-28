#include <gtest/gtest.h>

#include "Affect/DecisionRecord.h"
#include "Affect/DecisionRecorder.h"
#include "Affect/DecisionStore.h"
#include "Application/DeliveryReceiptFanout.h"
#include "MessageQueue/OutboundMessageQueue.h"
#include "MessageSender/QueuedMessageSender.h"
#include "Network/DeliveryReceiptRegistry.h"
#include "Perception/GroupContextStore.h"
#include "Perception/SelfMessageReceiptSink.h"
#include "Port/DeliveryReceipt.h"
#include "Protocol/OneBot/OneBotMessageEncoder.h"
#include "Protocol/OneBot/OneBotMessageId.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
class RecordingSink final : public DeliveryReceiptSink
{
public:
    void onReceipt(DeliveryReceipt receipt) override { receipts.push_back(std::move(receipt)); }
    std::vector<DeliveryReceipt> receipts;
};

class TemporaryDirectory
{
public:
    TemporaryDirectory()
    {
        std::string pattern = "/tmp/kleinbot-receipt-XXXXXX";
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

OneBotApiResult okResult(std::int64_t echo, nlohmann::json data)
{
    OneBotApiResult result;
    result.echo = echo;
    result.status = "ok";
    result.retcode = 0;
    result.data = std::move(data);
    return result;
}
} // namespace

// ---------- OutboundDelivery / 队列 / 编码器：无关联键时逐字不变 ----------

TEST(DeliveryCorrelationTest, DefaultsEmptyAndSurvivesQueue)
{
    OutboundDelivery plain{DirectMessageTarget{"42"}, TextMessage{"hi"}};
    EXPECT_TRUE(plain.correlation_id.empty());

    OutboundMessageQueue queue;
    QueuedMessageSender sender(queue);
    OutboundDelivery tracked{GroupMessageTarget{"88"}, TextMessage{"tracked"}};
    tracked.correlation_id = "exe_1";
    sender.deliver(tracked);
    sender.deliver(plain);

    auto first = queue.tryPop();
    auto second = queue.tryPop();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->correlation_id, "exe_1");
    EXPECT_TRUE(second->correlation_id.empty());
}

TEST(DeliveryCorrelationTest, EncoderIgnoresCorrelationAndEmitsNoEcho)
{
    OneBotMessageEncoder encoder;
    OutboundDelivery tracked{DirectMessageTarget{"42"}, TextMessage{"hello"}};
    tracked.correlation_id = "exe_1";
    const nlohmann::json encoded = encoder.encode(tracked).toJson();
    // echo 由传输层在写出时按需分配，编码契约本身不变
    EXPECT_FALSE(encoded.contains("echo"));
    EXPECT_EQ(encoded["action"], "send_private_msg");
}

// ---------- message_id 原样读取 ----------

TEST(OneBotMessageIdTest, ReadsNumberAndStringForms)
{
    EXPECT_EQ(oneBotMessageIdRaw(nlohmann::json{{"message_id", 123456}}), "123456");
    EXPECT_EQ(oneBotMessageIdRaw(nlohmann::json{{"message_id", -5}}), "-5");
    EXPECT_EQ(oneBotMessageIdRaw(nlohmann::json{{"message_id", "MsajdvLqdzYxMQA="}}),
              "MsajdvLqdzYxMQA=");
    EXPECT_EQ(oneBotMessageIdRaw(nlohmann::json{{"other", 1}}), "");
    EXPECT_EQ(oneBotMessageIdRaw(nlohmann::json()), "");
    EXPECT_EQ(oneBotMessageIdRaw(nlohmann::json::array()), "");
}

// ---------- DeliveryReceiptRegistry ----------

TEST(DeliveryReceiptRegistryTest, TrackAllocatesHighEchoSegmentAndResolvesSent)
{
    RecordingSink sink;
    DeliveryReceiptRegistry registry(&sink, [] { return 1000; });

    const std::int64_t echoA = registry.track("exe_a");
    const std::int64_t echoB = registry.track("exe_b");
    EXPECT_GE(echoA, DeliveryReceiptRegistry::kEchoBase);
    EXPECT_EQ(echoB, echoA + 1);
    EXPECT_EQ(registry.pendingCount(), 2U);

    EXPECT_TRUE(registry.resolve(okResult(echoB, nlohmann::json{{"message_id", 777}})));
    EXPECT_TRUE(registry.resolve(okResult(echoA, nlohmann::json{{"message_id", "s-1"}})));
    EXPECT_EQ(registry.pendingCount(), 0U);
    ASSERT_EQ(sink.receipts.size(), 2U);
    EXPECT_EQ(sink.receipts[0].correlation_id, "exe_b");
    EXPECT_EQ(sink.receipts[0].status, "sent");
    EXPECT_EQ(sink.receipts[0].message_id_raw, "777");
    EXPECT_EQ(sink.receipts[1].correlation_id, "exe_a");
    EXPECT_EQ(sink.receipts[1].message_id_raw, "s-1");
}

TEST(DeliveryReceiptRegistryTest, LowEchoBelongsToApiChannelNotRegistry)
{
    RecordingSink sink;
    DeliveryReceiptRegistry registry(&sink);
    registry.track("exe_a");

    // API 通道的 echo 从 1 起：本表必须放行（返回 false）且不发回执
    EXPECT_FALSE(registry.resolve(okResult(7, nlohmann::json::object())));
    EXPECT_TRUE(sink.receipts.empty());
    EXPECT_EQ(registry.pendingCount(), 1U);
}

TEST(DeliveryReceiptRegistryTest, FailedAndAsyncStatusesMapCorrectly)
{
    RecordingSink sink;
    DeliveryReceiptRegistry registry(&sink);

    const std::int64_t failedEcho = registry.track("exe_failed");
    OneBotApiResult failed;
    failed.echo = failedEcho;
    failed.status = "failed";
    failed.retcode = 1200;
    EXPECT_TRUE(registry.resolve(failed));

    const std::int64_t asyncEcho = registry.track("exe_async");
    OneBotApiResult async;
    async.echo = asyncEcho;
    async.status = "async";
    async.retcode = 0;
    EXPECT_TRUE(registry.resolve(async));

    ASSERT_EQ(sink.receipts.size(), 2U);
    EXPECT_EQ(sink.receipts[0].status, "failed");
    EXPECT_EQ(sink.receipts[0].retcode, 1200);
    EXPECT_EQ(sink.receipts[1].status, "sent");
    EXPECT_TRUE(sink.receipts[1].message_id_raw.empty());
}

TEST(DeliveryReceiptRegistryTest, SweepTimesOutAsUnknownAndLateFrameIsSwallowed)
{
    RecordingSink sink;
    std::int64_t now = 1000;
    DeliveryReceiptRegistry registry(&sink, [&now] { return now; }, 60);

    const std::int64_t echo = registry.track("exe_slow");
    now = 1059;
    registry.sweep();
    EXPECT_EQ(registry.pendingCount(), 1U);
    EXPECT_TRUE(sink.receipts.empty());

    now = 1060;
    registry.sweep();
    EXPECT_EQ(registry.pendingCount(), 0U);
    ASSERT_EQ(sink.receipts.size(), 1U);
    EXPECT_EQ(sink.receipts[0].correlation_id, "exe_slow");
    EXPECT_EQ(sink.receipts[0].status, "unknown");

    // 迟到的响应帧：属于本表 echo 段，吞掉（true）但不再发第二份回执
    EXPECT_TRUE(registry.resolve(okResult(echo, nlohmann::json{{"message_id", 1}})));
    EXPECT_EQ(sink.receipts.size(), 1U);
}

TEST(DeliveryReceiptRegistryTest, FailAllReportsUnknownForEveryPending)
{
    RecordingSink sink;
    DeliveryReceiptRegistry registry(&sink);
    registry.track("exe_1");
    registry.track("exe_2");

    registry.failAll("断连");
    EXPECT_EQ(registry.pendingCount(), 0U);
    ASSERT_EQ(sink.receipts.size(), 2U);
    for (const DeliveryReceipt &receipt : sink.receipts)
        EXPECT_EQ(receipt.status, "unknown");
    // 空表再 failAll 无副作用
    registry.failAll("再次");
    EXPECT_EQ(sink.receipts.size(), 2U);
}

TEST(DeliveryReceiptRegistryTest, NullSinkIsSafe)
{
    DeliveryReceiptRegistry registry(nullptr);
    const std::int64_t echo = registry.track("exe_1");
    EXPECT_TRUE(registry.resolve(okResult(echo, nlohmann::json::object())));
    registry.track("exe_2");
    registry.failAll("x");
    EXPECT_EQ(registry.pendingCount(), 0U);
}

// ---------- 扇出 ----------

TEST(DeliveryReceiptFanoutTest, BroadcastsToEverySink)
{
    RecordingSink first;
    RecordingSink second;
    DeliveryReceiptFanout fanout;
    fanout.addSink(&first);
    fanout.addSink(nullptr);
    fanout.addSink(&second);

    DeliveryReceipt receipt;
    receipt.correlation_id = "exe_1";
    receipt.status = "sent";
    fanout.onReceipt(receipt);
    ASSERT_EQ(first.receipts.size(), 1U);
    ASSERT_EQ(second.receipts.size(), 1U);
    EXPECT_EQ(second.receipts[0].correlation_id, "exe_1");
}

// ---------- affect 执行记录回填 ----------

TEST(ExecutionReceiptTest, QueuedAdvancesToSentWithMessageId)
{
    TemporaryDirectory directory;
    affect::DecisionStore store(directory.path() + "/affect.db");
    ASSERT_TRUE(store.isOpen());
    affect::DecisionRecorder recorder(&store, [] { return 1234; });

    const std::string decisionId = recorder.recordDecision(
        affect::makeBaselineDecision("at_reply", 8823, 10001, "msg-1", "reply"));
    ASSERT_FALSE(decisionId.empty());
    const std::string executionId = affect::DecisionRecorder::newExecutionId();
    EXPECT_NE(executionId.find("exe_"), std::string::npos);
    affect::ExecutionRecord execution;
    execution.executionId = executionId;
    execution.decisionId = decisionId;
    execution.status = "queued";
    execution.actualText = "在呢";
    ASSERT_TRUE(recorder.recordExecution(execution));

    DeliveryReceipt receipt;
    receipt.correlation_id = executionId;
    receipt.status = "sent";
    receipt.message_id_raw = "987654";
    static_cast<DeliveryReceiptSink &>(recorder).onReceipt(receipt);

    const auto found = store.findExecution(executionId);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->status, "sent");
    EXPECT_EQ(found->outboundMessageId, "987654");
    EXPECT_EQ(found->actualText, "在呢");
    EXPECT_EQ(found->decisionId, decisionId);
}

TEST(ExecutionReceiptTest, UnknownNeverDowngradesAndLateDefiniteReceiptUpgrades)
{
    TemporaryDirectory directory;
    affect::DecisionStore store(directory.path() + "/affect.db");
    ASSERT_TRUE(store.isOpen());

    affect::ExecutionRecord execution;
    execution.executionId = "exe-fixed";
    execution.decisionId = "dec-fixed";
    execution.createdAt = 1;
    execution.status = "queued";
    ASSERT_TRUE(store.appendExecution(execution));

    // queued → unknown（超时）
    EXPECT_TRUE(store.updateExecutionReceipt("exe-fixed", "unknown", ""));
    EXPECT_EQ(store.findExecution("exe-fixed")->status, "unknown");
    // unknown → sent（迟到的明确回执优先）
    EXPECT_TRUE(store.updateExecutionReceipt("exe-fixed", "sent", "42"));
    EXPECT_EQ(store.findExecution("exe-fixed")->status, "sent");
    EXPECT_EQ(store.findExecution("exe-fixed")->outboundMessageId, "42");
    // sent 之后的 unknown 不能把它退化
    EXPECT_FALSE(store.updateExecutionReceipt("exe-fixed", "unknown", ""));
    EXPECT_EQ(store.findExecution("exe-fixed")->status, "sent");
    EXPECT_EQ(store.findExecution("exe-fixed")->outboundMessageId, "42");
    // sent 之后再来 failed 也不动（状态已定）
    EXPECT_FALSE(store.updateExecutionReceipt("exe-fixed", "failed", ""));
    EXPECT_EQ(store.findExecution("exe-fixed")->status, "sent");

    // 空 message_id 的回执不擦掉已有 id；非法状态与未知 id 拒绝
    affect::ExecutionRecord another = execution;
    another.executionId = "exe-2";
    ASSERT_TRUE(store.appendExecution(another));
    EXPECT_TRUE(store.updateExecutionReceipt("exe-2", "failed", ""));
    EXPECT_EQ(store.findExecution("exe-2")->status, "failed");
    EXPECT_TRUE(store.findExecution("exe-2")->outboundMessageId.empty());
    EXPECT_FALSE(store.updateExecutionReceipt("exe-2", "weird", ""));
    EXPECT_FALSE(store.updateExecutionReceipt("missing", "sent", "1"));
    EXPECT_FALSE(store.findExecution("missing").has_value());
}

TEST(ExecutionReceiptTest, DisabledRecorderIgnoresReceipts)
{
    affect::DecisionRecorder recorder(nullptr);
    DeliveryReceipt receipt;
    receipt.correlation_id = "exe_x";
    receipt.status = "sent";
    // 不崩、不抛
    static_cast<DeliveryReceiptSink &>(recorder).onReceipt(receipt);
    EXPECT_FALSE(recorder.enabled());
}

// ---------- 群自消息 message_id 回填 ----------

TEST(SelfMessageBackfillTest, BackfillUpdatesMirrorAndDiskOnlyForSelfRecords)
{
    TemporaryDirectory directory;
    const std::string dbPath = directory.path() + "/ctx.db";
    {
        GroupContextStore store(dbPath);
        ASSERT_TRUE(store.isOpen());

        GroupMessageRecord other;
        other.groupId = 88;
        other.userId = 10001;
        other.nickname = "A";
        other.text = "hi";
        other.timestamp = 100;
        other.correlationId = "exe_self"; // 非自消息即使撞键也不回填
        store.append(other);

        GroupMessageRecord self;
        self.groupId = 88;
        self.userId = 10000;
        self.nickname = "Klein";
        self.text = "在呢";
        self.timestamp = 101;
        self.isSelf = true;
        self.correlationId = "exe_self";
        store.append(self);

        EXPECT_FALSE(store.backfillMessageId("exe_missing", "1"));
        EXPECT_FALSE(store.backfillMessageId("exe_self", ""));
        EXPECT_TRUE(store.backfillMessageId("exe_self", "556677"));

        const auto snapshot = store.snapshot(88);
        ASSERT_EQ(snapshot.size(), 2U);
        EXPECT_TRUE(snapshot[0].messageId.empty());
        EXPECT_EQ(snapshot[1].messageId, "556677");
        EXPECT_TRUE(snapshot[1].isSelf);
    }
    // 重启重建：回填已落盘
    GroupContextStore reopened(dbPath);
    const auto snapshot = reopened.snapshot(88);
    ASSERT_EQ(snapshot.size(), 2U);
    EXPECT_EQ(snapshot[1].messageId, "556677");
    EXPECT_TRUE(snapshot[1].correlationId.empty()); // 关联键不落库
}

TEST(SelfMessageBackfillTest, SinkOnlyActsOnSentWithMessageId)
{
    TemporaryDirectory directory;
    GroupContextStore store(directory.path() + "/ctx.db");
    ASSERT_TRUE(store.isOpen());
    GroupMessageRecord self;
    self.groupId = 88;
    self.userId = 10000;
    self.nickname = "Klein";
    self.text = "在呢";
    self.timestamp = 101;
    self.isSelf = true;
    self.correlationId = "exe_self";
    store.append(self);

    SelfMessageReceiptSink sink(&store);
    DeliveryReceipt unknown;
    unknown.correlation_id = "exe_self";
    unknown.status = "unknown";
    sink.onReceipt(unknown);
    EXPECT_TRUE(store.snapshot(88)[0].messageId.empty());

    DeliveryReceipt sentNoId;
    sentNoId.correlation_id = "exe_self";
    sentNoId.status = "sent";
    sink.onReceipt(sentNoId);
    EXPECT_TRUE(store.snapshot(88)[0].messageId.empty());

    DeliveryReceipt sent = sentNoId;
    sent.message_id_raw = "99";
    sink.onReceipt(sent);
    EXPECT_EQ(store.snapshot(88)[0].messageId, "99");

    // store 为空：no-op 不崩
    SelfMessageReceiptSink nullSink(nullptr);
    nullSink.onReceipt(sent);
}

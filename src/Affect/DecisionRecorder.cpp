#include "DecisionRecorder.h"

#include "../Log/Log.h"
#include "../utils/UniqueId.h"

#include <ctime>

namespace affect
{
namespace
{
std::string makeId(const char *prefix)
{
    return utils::uniqueId(prefix);
}
} // namespace

DecisionRecorder::DecisionRecorder(DecisionStore *store,
                                   std::function<std::int64_t()> clock)
    : store(store),
      clock(clock ? std::move(clock)
                  : [] { return static_cast<std::int64_t>(std::time(nullptr)); })
{
}

bool DecisionRecorder::enabled() const
{
    return store != nullptr && store->isOpen();
}

std::string DecisionRecorder::recordDecision(DecisionRecord record)
{
    if (!enabled())
        return {};

    if (record.decisionId.empty())
        record.decisionId = makeId("dec");
    if (record.createdAt == 0)
        record.createdAt = clock();
    if (!store->appendDecision(record))
        return {};
    return record.decisionId;
}

bool DecisionRecorder::recordExecution(ExecutionRecord record)
{
    if (!enabled())
        return false;

    if (record.executionId.empty())
        record.executionId = makeId("exe");
    if (record.createdAt == 0)
        record.createdAt = clock();
    return store->appendExecution(record);
}

bool DecisionRecorder::recordFeedback(FeedbackRecord record)
{
    if (!enabled())
        return false;

    if (record.feedbackId.empty())
        record.feedbackId = makeId("fbk");
    if (record.createdAt == 0)
        record.createdAt = clock();
    return store->appendFeedback(record);
}

std::string DecisionRecorder::newExecutionId()
{
    return makeId("exe");
}

std::string DecisionRecorder::newDecisionId()
{
    return makeId("dec");
}

void DecisionRecorder::onReceipt(DeliveryReceipt receipt)
{
    if (!enabled() || receipt.correlation_id.empty())
        return;
    std::string error;
    // 分段投递："execution_id#序号" 只把该段的 message_id 聚合到同一条执行记录，
    // 执行状态由首段（裸 execution_id）驱动；失败/未知的分段回执不改状态
    const std::size_t hash = receipt.correlation_id.find('#');
    if (hash != std::string::npos)
    {
        if (receipt.status == "sent" && !receipt.message_id_raw.empty())
        {
            const std::string executionId = receipt.correlation_id.substr(0, hash);
            if (!store->appendExecutionMessageId(executionId, receipt.message_id_raw, &error))
                LOG_DEBUG("分段回执未落库：" + error);
        }
        return;
    }
    if (!store->updateExecutionReceipt(receipt.correlation_id, receipt.status,
                                       receipt.message_id_raw, &error))
    {
        // 查无此行通常是无记录器时期的投递或 correlation 不是 execution_id；
        // 状态已定（sent 后又来 unknown）也在此静默
        LOG_DEBUG("执行回执未落库：" + error);
        return;
    }
    LOG_DEBUG("执行回执：" + receipt.correlation_id + " → " + receipt.status +
              (receipt.message_id_raw.empty() ? std::string()
                                              : "，message_id=" + receipt.message_id_raw));
}
} // namespace affect

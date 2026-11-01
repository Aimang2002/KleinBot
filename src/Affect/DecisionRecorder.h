#ifndef AFFECT_DECISION_RECORDER_H
#define AFFECT_DECISION_RECORDER_H

#include "DecisionStore.h"
#include "../Port/DeliveryReceipt.h"

#include <cstdint>
#include <functional>
#include <string>

namespace affect
{
// 决策记录器：生成稳定 ID、填时间戳、委托 DecisionStore 写入。
// 两条群回应路径共享同一实例；store 为空或未打开时 enabled()=false，
// 调用方据此保持 v2.4.1 行为（不写任何新数据）。
//
// 同时是出站回执的消费端（R0.2 / B2）：投递的 correlation_id 就是 execution_id，
// 回执到达时把 queued 推进到 sent/failed/unknown 并回填真实 message_id。
// 调用方必须先 recordExecution 再投递，保证回执到达时行已存在
class DecisionRecorder : public DeliveryReceiptSink
{
public:
    explicit DecisionRecorder(DecisionStore *store = nullptr,
                              std::function<std::int64_t()> clock = {});

    bool enabled() const;

    // 返回实际写入的 decision_id；失败返回空串。
    std::string recordDecision(DecisionRecord record);
    bool recordExecution(ExecutionRecord record);
    bool recordFeedback(FeedbackRecord record);

    // 预生成 execution_id：调用方先拿 id 作投递 correlation_id，再 recordExecution
    static std::string newExecutionId();
    // 预生成 decision_id（R1 @ 路径影子期）：执行记录先引用它落库、投递，judge 跑完后
    // 再以同一 id recordDecision（record.decisionId 非空时原样写入）
    static std::string newDecisionId();

    // DeliveryReceiptSink：传输线程调用，只做一次短 UPDATE；recorder 关闭时 no-op
    void onReceipt(DeliveryReceipt receipt) override;

private:
    DecisionStore *store;
    std::function<std::int64_t()> clock;
};
} // namespace affect

#endif // AFFECT_DECISION_RECORDER_H

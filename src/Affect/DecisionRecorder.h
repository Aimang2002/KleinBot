#ifndef AFFECT_DECISION_RECORDER_H
#define AFFECT_DECISION_RECORDER_H

#include "AffectOptions.h"
#include "DecisionStore.h"
#include "../Port/DeliveryReceipt.h"
#include "../utils/Live.h"

#include <cstdint>
#include <functional>
#include <string>

namespace affect
{
// 决策记录器：生成稳定 ID、填时间戳、委托 DecisionStore 写入。
// 两条群回应路径共享同一实例。稳定外壳（ADR-023）：记录器与决策库始终构造，
// enabled() 每次读活值里的 affect.enabled——为真时才（第一次）打开决策库；
// 关着或库打不开时 enabled()=false，调用方据此保持 v2.4.1 行为（不写任何新数据）。
// 各 record* 同样先问 enabled()。
//
// 同时是出站回执的消费端（R0.2 / B2）：投递的 correlation_id 就是 execution_id，
// 回执到达时把 queued 推进到 sent/failed/unknown 并回填真实 message_id。
// 调用方必须先 recordExecution 再投递，保证回执到达时行已存在
class DecisionRecorder : public DeliveryReceiptSink
{
public:
    // store、options 由组合根持有、须比本对象活得久
    DecisionRecorder(DecisionStore &store, const Live<AffectOptions> &options,
                     std::function<std::int64_t()> clock = {});

    // 当前 affect.enabled 为真且决策库可用（第一次为真时打开决策库）
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

    // DeliveryReceiptSink：传输线程调用，只做一次短 UPDATE；决策库没打开过时 no-op
    //（不因回执打开数据库）。affect 中途关掉后，已落库的执行行仍照常回填回执
    void onReceipt(DeliveryReceipt receipt) override;

private:
    DecisionStore &store;
    const Live<AffectOptions> &options;
    std::function<std::int64_t()> clock;
};
} // namespace affect

#endif // AFFECT_DECISION_RECORDER_H

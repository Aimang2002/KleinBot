#ifndef AFFECT_DECISION_RECORDER_H
#define AFFECT_DECISION_RECORDER_H

#include "DecisionStore.h"

#include <cstdint>
#include <functional>
#include <string>

namespace affect
{
// 决策记录器：生成稳定 ID、填时间戳、委托 DecisionStore 写入。
// 两条群回应路径共享同一实例；store 为空或未打开时 enabled()=false，
// 调用方据此保持 v2.4.1 行为（不写任何新数据）。
class DecisionRecorder
{
public:
    explicit DecisionRecorder(DecisionStore *store = nullptr,
                              std::function<std::int64_t()> clock = {});

    bool enabled() const;

    // 返回实际写入的 decision_id；失败返回空串。
    std::string recordDecision(DecisionRecord record);
    bool recordExecution(ExecutionRecord record);
    bool recordFeedback(FeedbackRecord record);

private:
    DecisionStore *store;
    std::function<std::int64_t()> clock;
};
} // namespace affect

#endif // AFFECT_DECISION_RECORDER_H

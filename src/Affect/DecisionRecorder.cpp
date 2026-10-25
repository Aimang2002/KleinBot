#include "DecisionRecorder.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <mutex>
#include <random>
#include <sstream>

namespace affect
{
namespace
{
std::string makeId(const char *prefix)
{
    static std::atomic<std::uint64_t> counter{0};
    static std::mutex randomMutex;
    static std::mt19937_64 engine(std::random_device{}());

    std::uint64_t randomValue = 0;
    {
        std::lock_guard<std::mutex> lock(randomMutex);
        randomValue = engine();
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();

    std::ostringstream output;
    output << prefix << '_' << std::hex
           << static_cast<std::uint64_t>(now) << '_'
           << counter.fetch_add(1) << '_' << randomValue;
    return output.str();
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
} // namespace affect

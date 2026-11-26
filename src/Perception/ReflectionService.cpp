#include "ReflectionService.h"

#include "GroupContextStore.h"
#include "GroupLineFormat.h"
#include "../Affect/DecisionRecorder.h"
#include "../Affect/DecisionStore.h"
#include "../Affect/FactsCard.h"
#include "../Affect/KnowledgeGuard.h"
#include "../Affect/KnowledgeLedger.h"
#include "../Log/Log.h"
#include "../utils/LocalDate.h"
#include "../utils/UniqueId.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <map>

namespace affect
{
namespace
{
constexpr std::int64_t kPumpIntervalSeconds = 60;
constexpr std::size_t kScanLimit = 60; // 一次最多扫多少条决策找经历（游标之后）

std::string metaKey(std::uint64_t groupId)
{
    return "affect.reflection." + std::to_string(groupId);
}

bool spokenExecution(const ExecutionRecord &execution)
{
    return execution.status != "skipped" && execution.status != "failed" && !execution.actualText.empty();
}
} // namespace

ReflectionService::ReflectionService(DecisionStore &store, DecisionRecorder &recorder,
                                     KnowledgeLedger &ledger, const GroupContextStore *groups,
                                     const Live<AffectOptions> &affectOptions,
                                     const Live<BotIdentity> &bot, TargetResolver resolver,
                                     std::function<std::int64_t()> clock)
    : store(store), recorder(recorder), ledger(ledger), groups(groups), affectOptions(affectOptions),
      botIdentity(bot), resolver(std::move(resolver)),
      clock(clock ? std::move(clock)
                  : std::function<std::int64_t()>([] { return static_cast<std::int64_t>(std::time(nullptr)); }))
{
}

ReflectionService::~ReflectionService()
{
    shutdown();
}

bool ReflectionService::enabled() const
{
    return affectOptions.get()->enabled;
}

void ReflectionService::start()
{
    std::lock_guard<std::mutex> lock(queueMutex);
    if (started || stopping)
        return;
    started = true;
    worker = std::thread([this] { workerLoop(); });
}

void ReflectionService::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (stopping)
            return;
        stopping = true;
        queue.clear();
        queued.clear();
    }
    queueChanged.notify_all();
    if (worker.joinable())
        worker.join();
}

void ReflectionService::workerLoop()
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            queueChanged.wait(lock, [this] { return stopping || !queue.empty(); });
            if (stopping)
                return;
            job = queue.front();
            queue.pop_front();
        }
        try
        {
            runNow(job.groupId, job.manual);
        }
        catch (const std::exception &exception)
        {
            LOG_WARNING("回想：群 " + std::to_string(job.groupId) + " 异常：" + exception.what());
        }
        std::lock_guard<std::mutex> lock(queueMutex);
        queued.erase(job.groupId);
    }
}

ReflectionService::QueueState ReflectionService::enqueue(std::uint64_t groupId, bool manual)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (stopping)
            return QueueState::Stopped;
        if (queued.count(groupId) != 0)
        {
            // 已排队：手动触发只把那一次升级成手动（不受当日帽限制）。queued 里有、队列里没有 =
            // 工作线程已取走、正在回想（回想完才从 queued 里删）
            bool waiting = false;
            for (Job &job : queue)
                if (job.groupId == groupId)
                {
                    job.manual = job.manual || manual;
                    waiting = true;
                }
            return waiting ? QueueState::Waiting : QueueState::Running;
        }
        queued.insert(groupId);
        queue.push_back({groupId, manual});
    }
    queueChanged.notify_one();
    return QueueState::Added;
}

void ReflectionService::pump(std::int64_t now)
{
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (stopping || !started)
            return;
    }
    if (now - lastPump < kPumpIntervalSeconds)
        return;
    lastPump = now;
    for (const std::uint64_t groupId : dueGroups(now))
        enqueue(groupId, false);
}

ReflectionRequest ReflectionService::requestNow(std::uint64_t groupId)
{
    ReflectionRequest request;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        request.unavailable = stopping;
    }
    if (request.unavailable || !enabled() || !ledger.open())
    {
        request.unavailable = true;
        return request;
    }
    std::vector<std::uint64_t> candidates;
    if (groupId != 0)
        candidates.push_back(groupId);
    else
        candidates = store.decisionGroups();
    const std::int64_t now = clock();
    for (const std::uint64_t candidate : candidates)
    {
        // 已排队或正在回想的群先如实计数，不看有没有经历（正在回想时游标还没前移，经历看起来仍是
        // 待回想的）；其余的群与自动调度同一判据：游标套 24 小时地板后还有后续窗口已关闭的决策
        if (!isQueued(candidate))
        {
            ReflectionProgress progress = loadProgress(candidate);
            floorCursor(progress, now);
            ReflectionProgress scanned;
            if (pendingExperiences(candidate, progress, now, 1, scanned).empty())
                continue;
        }
        switch (enqueue(candidate, true)) // 已在队列里的那一次升级成手动
        {
        case QueueState::Added:
            ++request.scheduled;
            break;
        case QueueState::Waiting:
            ++request.waiting;
            break;
        case QueueState::Running:
            ++request.running;
            break;
        case QueueState::Stopped:
            request.unavailable = true;
            return request;
        }
    }
    return request;
}

bool ReflectionService::isQueued(std::uint64_t groupId) const
{
    std::lock_guard<std::mutex> lock(queueMutex);
    return queued.count(groupId) != 0;
}

ReflectionProgress ReflectionService::loadProgress(std::uint64_t groupId) const
{
    ReflectionProgress progress;
    const std::string raw = ledger.readMeta(metaKey(groupId));
    if (raw.empty())
        return progress;
    try
    {
        const nlohmann::json value = nlohmann::json::parse(raw);
        if (value.is_object())
        {
            progress.cursor = value.value("cursor", std::int64_t{0});
            // 旧进度没有这个字段：空 = cursor 那一秒的都看过了（与加字段之前的 created_at > cursor 一致）
            progress.cursorDecision = value.value("cursor_decision", std::string());
            progress.lastRun = value.value("last_run", std::int64_t{0});
            progress.day = value.value("day", std::int64_t{0});
            progress.runsToday = value.value("runs_today", 0);
            progress.retryAfter = value.value("retry_after", std::int64_t{0});
        }
    }
    catch (const std::exception &)
    {
        LOG_WARNING("回想：群 " + std::to_string(groupId) + " 的进度记录不可读，按从头算");
    }
    return progress;
}

bool ReflectionService::saveProgress(std::uint64_t groupId, const ReflectionProgress &progress)
{
    const nlohmann::json value = {{"cursor", progress.cursor},
                                  {"cursor_decision", progress.cursorDecision},
                                  {"last_run", progress.lastRun},
                                  {"day", progress.day},
                                  {"runs_today", progress.runsToday},
                                  {"retry_after", progress.retryAfter}};
    return ledger.writeMeta(metaKey(groupId), value.dump());
}

ReflectionProgress ReflectionService::progressOf(std::uint64_t groupId) const
{
    return loadProgress(groupId);
}

// 游标地板：不早于 24 小时前（群缓冲的保留期）。更早的决策没有现场可看，也不该让第一次回想从头翻旧账。
// 地板高于游标时，游标那一秒的决策号随之作废（地板那一秒按“都看过了”算）
void ReflectionService::floorCursor(ReflectionProgress &progress, std::int64_t now)
{
    const std::int64_t floor = now - ReflectionRules::kLookbackSeconds;
    if (progress.cursor < floor)
    {
        progress.cursor = floor;
        progress.cursorDecision.clear();
    }
}

std::vector<ReflectionExperience> ReflectionService::pendingExperiences(
    std::uint64_t groupId, const ReflectionProgress &from, std::int64_t now, std::size_t limit,
    ReflectionProgress &scanned) const
{
    std::vector<ReflectionExperience> experiences;
    scanned.cursor = from.cursor;
    scanned.cursorDecision = from.cursorDecision;
    const std::int64_t until = now - ReflectionRules::kAftermathSeconds;
    if (limit == 0 || until < from.cursor || (until == from.cursor && from.cursorDecision.empty()))
        return experiences;
    for (const DecisionRecord &decision :
         store.decisionsForGroup(groupId, from.cursor, until, kScanLimit, from.cursorDecision))
    {
        scanned.cursor = decision.createdAt;
        scanned.cursorDecision = decision.decisionId;
        const std::vector<ExecutionRecord> executions = store.executionsFor(decision.decisionId);
        // 每个动作都是经历（2026-11-21 部署者定规）：开口、沉默、离场都回想
        const auto spoken = std::find_if(executions.begin(), executions.end(), spokenExecution);
        ReflectionExperience experience;
        experience.decisionId = decision.decisionId;
        experience.spoke = spoken != executions.end();
        experience.at = experience.spoke && spoken->createdAt != 0 ? spoken->createdAt : decision.createdAt;
        experience.action = decision.plan.action;
        experience.shadow = std::any_of(executions.begin(), executions.end(),
                                        [](const ExecutionRecord &execution)
                                        { return execution.deviationReason == "shadow"; });
        experience.timeText = groupClock(experience.at);
        experience.source = decision.source;
        if (decision.plan.isV2())
        {
            experience.occasion = decision.plan.occasion;
            experience.move = decision.plan.move;
        }
        if (experience.spoke)
            experience.spoken = spoken->actualText;
        for (const FeedbackRecord &feedback : store.feedbackFor(decision.decisionId))
        {
            try
            {
                const nlohmann::json evidence = nlohmann::json::parse(feedback.evidenceJson);
                const std::string reading = evidence.is_object() ? evidence.value("reading", "") : "";
                if (!reading.empty())
                    experience.readings.emplace_back(feedback.feedbackId, reading);
            }
            catch (const std::exception &)
            {
            }
        }
        experiences.push_back(std::move(experience));
        if (experiences.size() >= limit)
            break;
    }
    return experiences;
}

std::vector<std::uint64_t> ReflectionService::dueGroups(std::int64_t now)
{
    std::vector<std::uint64_t> due;
    if (!enabled() || !ledger.open())
        return due;
    const std::int64_t today = static_cast<std::int64_t>(localDayStart(static_cast<std::time_t>(now)));
    for (const std::uint64_t groupId : store.decisionGroups())
    {
        ReflectionProgress progress = loadProgress(groupId);
        const int runsToday = progress.day == today ? progress.runsToday : 0;
        if (runsToday >= ReflectionRules::kDailyCap)
            continue;
        if (now < progress.retryAfter)
            continue; // 上次模型没返回：退避中
        floorCursor(progress, now);
        ReflectionProgress scanned;
        const std::vector<ReflectionExperience> pending =
            pendingExperiences(groupId, progress, now, 1, scanned);
        if (pending.empty())
            continue; // 每个决策都是经历：没有经历 = 游标之后没有后续窗口已关闭的决策，游标不用动

        bool early = false;
        if (groups != nullptr)
        {
            std::size_t after = 0;
            for (const GroupMessageRecord &record : groups->snapshot(groupId))
                if (record.timestamp > pending.front().at)
                    ++after;
            early = after >= ReflectionRules::kEarlyTriggerMessages;
        }
        if (early || now - progress.lastRun >= ReflectionRules::kIntervalSeconds)
            due.push_back(groupId);
    }
    return due;
}

ReflectionReport ReflectionService::runNow(std::uint64_t groupId, bool manual)
{
    ReflectionReport report;
    if (!enabled())
    {
        report.skipped = "affect 关着";
        return report;
    }
    if (!ledger.open())
    {
        report.skipped = "账本打不开";
        return report;
    }
    const std::int64_t now = clock();
    ReflectionProgress progress = loadProgress(groupId);
    floorCursor(progress, now);
    ReflectionProgress scanned;
    std::vector<ReflectionExperience> experiences =
        pendingExperiences(groupId, progress, now, ReflectionRules::kExperiencesPerRun, scanned);
    if (experiences.empty())
    {
        report.skipped = "没有待回想的经历";
        return report;
    }

    // ---- 装配材料（全部在内存里）----
    const auto bot = botIdentity.get();
    BotIdentity identity = *bot;
    ReflectionInput input;
    input.groupId = groupId;
    input.botName = identity.name;
    input.soul = soulSource ? soulSource() : std::string();
    input.deployerFacts = factsSource ? factsSource() : std::string();
    input.sceneNote = sceneFactsNote(static_cast<std::time_t>(now));

    std::vector<GroupMessageRecord> records = groups != nullptr ? groups->snapshot(groupId)
                                                                 : std::vector<GroupMessageRecord>();
    const GroupReplyIndex replies(records);
    std::map<std::uint64_t, std::string> people; // 材料里出现过的群友（不含她自己）
    std::vector<std::string> material;           // 全部现场原文：挡照抄用
    std::set<std::uint64_t> presentUsers;
    std::int64_t shownUpTo = 0;                  // 经历现场覆盖到的时刻
    for (const GroupMessageRecord &record : records)
        if (identity.managerId != 0 && record.userId == identity.managerId && !record.isSelf)
            input.managerNickname = record.nickname;
    for (ReflectionExperience &experience : experiences)
    {
        std::size_t taken = 0;
        for (const GroupMessageRecord &record : records)
        {
            if (record.timestamp <= experience.at)
                continue;
            if (taken >= ReflectionRules::kAftermathMessages)
                break;
            experience.aftermath.push_back({formatGroupLine(identity, record, replies), record.text});
            material.push_back(record.text);
            shownUpTo = std::max(shownUpTo, record.timestamp);
            if (!record.isSelf && record.userId != 0)
            {
                people[record.userId] = record.nickname;
                presentUsers.insert(record.userId);
            }
            ++taken;
        }
        shownUpTo = std::max(shownUpTo, experience.at);
    }
    // 她没参与的一段群聊：经历现场之后、不是她说的，最近的至多 60 条
    std::vector<const GroupMessageRecord *> bystander;
    for (const GroupMessageRecord &record : records)
        if (record.timestamp > shownUpTo && !record.isSelf)
            bystander.push_back(&record);
    if (bystander.size() > ReflectionRules::kBystanderMessages)
        bystander.erase(bystander.begin(),
                        bystander.begin() + static_cast<std::ptrdiff_t>(bystander.size() -
                                                                        ReflectionRules::kBystanderMessages));
    for (const GroupMessageRecord *record : bystander)
    {
        input.bystander.push_back({formatGroupLine(identity, *record, replies), record->text});
        material.push_back(record->text);
        if (record->userId != 0)
        {
            people[record->userId] = record->nickname;
            presentUsers.insert(record->userId);
        }
    }
    for (const auto &[userId, nickname] : people)
        input.people.push_back({userId, nickname});
    for (const RecalledKnowledge &item : ledger.recallPool(groupId))
    {
        input.knowledge.push_back({item.entry.id, item.entry.kind, item.entry.scopeGroup,
                                   item.entry.scopeUser, item.entry.text, item.teaching});
    }
    std::vector<std::string> decisionIds;
    for (const ReflectionExperience &experience : experiences)
        decisionIds.push_back(experience.decisionId);
    input.experiences = experiences;
    report.experiences = experiences.size();

    // ---- 调模型（锁外）----
    const Target target = resolver ? resolver() : Target{};
    report.modelLabel = target.modelLabel;
    if (!target.worker)
    {
        report.skipped = "模型未接入";
        return report;
    }
    const auto started = std::chrono::steady_clock::now();
    std::string raw;
    try
    {
        raw = target.worker(reflectionSystemPrompt(), reflectionUserPrompt(input));
    }
    catch (const std::exception &exception)
    {
        LOG_WARNING("回想：群 " + std::to_string(groupId) + " 模型调用异常：" + exception.what());
        raw.clear();
    }
    report.latencyMs = static_cast<long>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                             std::chrono::steady_clock::now() - started)
                                             .count());
    report.ran = true;
    if (raw.empty())
    {
        // 超时、取消或失败：这次不算，游标不动；自动回想退避 30 分钟再试（手动不受限）
        LOG_WARNING("回想：群 " + std::to_string(groupId) + " 模型没有返回（" + report.modelLabel + "，" +
                    std::to_string(report.latencyMs) + " ms），这次不算，30 分钟后再试");
        progress.retryAfter = now + ReflectionRules::kRetrySeconds;
        saveProgress(groupId, progress);
        report.skipped = "模型没有返回";
        return report;
    }
    report.runId = utils::uniqueId("reflect");

    std::string error;
    std::optional<ReflectionOutput> output = parseReflection(raw, error);
    if (!output)
    {
        // 模型给了东西但不可用：记日志，这几次经历不再重看（否则会卡在同一批上）
        LOG_WARNING("回想：群 " + std::to_string(groupId) + " 输出不可用（" + error + "），经历 " +
                    std::to_string(experiences.size()) + " 次跳过");
        output = ReflectionOutput{};
    }

    // ---- 读法 → 反馈 ----
    for (const ReflectionReading &reading : output->readings)
    {
        std::string reason;
        if (!checkReading(reading, decisionIds, material, reason))
        {
            ++report.rejected;
            LOG_INFO("回想：群 " + std::to_string(groupId) + " 一条读法没收（" + reason + "）");
            continue;
        }
        FeedbackRecord feedback;
        feedback.decisionId = reading.decisionId;
        feedback.source = "self_reflection";
        feedback.status = "known";
        feedback.confidence = reading.confidence;
        feedback.evidenceJson = nlohmann::json{{"reading", reading.reading},
                                               {"polarity", reading.polarity},
                                               {"run_id", report.runId},
                                               {"model", report.modelLabel}}
                                    .dump();
        if (recorder.recordFeedback(std::move(feedback)))
            ++report.readings;
    }

    // ---- 认识的增改 → 账本 ----
    if (!output->changes.empty())
    {
        std::vector<KnowledgeChange> changes = output->changes;
        for (KnowledgeChange &change : changes)
        {
            if (change.targetId == 0 && change.key.empty())
                change.key = reflectionKey(change.text);
            if (change.evidence.empty())
            {
                // 没写依据就按这次看的经历算：改动确实出自它们
                for (const std::string &id : decisionIds)
                    change.evidence.push_back("decision:" + id);
            }
        }
        KnowledgeRunContext context;
        context.origin = "reflection";
        context.groupId = groupId;
        context.presentUsers = presentUsers;
        context.material = material;
        context.evidenceExists = evidenceCheckerFor(store);
        context.runId = report.runId;
        context.note = {{"by", "reflection"}, {"model", report.modelLabel}};
        report.decayed = ledger.applyDecay();
        const KnowledgeRunResult result = ledger.apply(changes, context);
        if (!result.ok)
            LOG_WARNING("回想：群 " + std::to_string(groupId) + " 账本写入失败：" + result.error);
        report.written = result.written.size();
        report.rejected += result.rejected.size();
    }

    // ---- 进度 ----
    const std::int64_t today = static_cast<std::int64_t>(localDayStart(static_cast<std::time_t>(now)));
    if (progress.day != today)
    {
        progress.day = today;
        progress.runsToday = 0;
    }
    if (!manual)
        ++progress.runsToday;
    progress.lastRun = now;
    progress.retryAfter = 0;
    // 游标推到这次看过的最后一条决策：同一秒里号更大的决策留给下一次
    progress.cursor = scanned.cursor;
    progress.cursorDecision = scanned.cursorDecision;
    saveProgress(groupId, progress);

    LOG_INFO("回想：群 " + std::to_string(groupId) + " 运行 " + report.runId + "（" +
             (manual ? "手动" : "自动") + "，" + report.modelLabel + "，" + std::to_string(report.latencyMs) +
             " ms）：经历 " + std::to_string(report.experiences) + " 次，读法 " +
             std::to_string(report.readings) + " 条，账本写入 " + std::to_string(report.written) +
             " 条，挡掉 " + std::to_string(report.rejected) + " 条，遗忘 " + std::to_string(report.decayed) +
             " 条");
    return report;
}
} // namespace affect

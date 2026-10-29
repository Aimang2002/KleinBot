#ifndef GLANCE_SERVICE_H
#define GLANCE_SERVICE_H

/*
 * 群聊瞥视（R1.1，ADR-020 群聊视觉感知分层的"瞥视层"）：观察路径入库的每张
 * 群图片异步交给轻量视觉模型"瞥一眼"，产出一行派生描述，写回群缓冲里该条
 * 消息的图片占位符：
 *     [image asset_id=gimg_x source=inbound]
 *  →  [image asset_id=gimg_x source=inbound | 表情包：一只猫趴在键盘上，搞笑]
 * judge / 主动轮 / 召回 / 摘要读的都是 group_messages.text，于是自动获得"眼睛"。
 * 需要细节时仍由模型按需调用 inspect_image（细看层）。
 *
 * 零行为变化：本服务只丰富文本，不触发任何回应、不改任何决策。规则只允许
 * 落在资源预算与安全护栏上——每群每分钟预算、队列封顶、按字节去重；
 * 不允许任何"看到图就做 X"的行为逻辑。
 *
 * 线程：一个后台 worker（start() 且启用、有视觉 seam、有群缓冲时才起）。
 * enqueue 在消息 worker 的观察路径上调用，只做锁内入队，不阻塞、不做 IO；
 * 文件读取、视觉 HTTP、写回 SQLite 全在本 worker 内，且都在锁外。
 * 队列满时丢最旧任务并（由 worker 稍后）标记"未看（刷屏）"；shutdown 丢弃
 * 全部 pending 不写标记。进行中的视觉 HTTP 经 Dock 的 CurlRequestControl 随
 * running=false 自动取消，worker 在视觉返回后检查 stopping 再决定是否写回。
 *
 * 描述是派生文本：落 group_messages.text，随群缓冲 24h/每群 300 条淘汰，
 * 不进长期记忆、不进个人库；视觉输出按不可信数据清洗（单行、去 | 与 ]、
 * ≤80 码点），不做语义过滤。
 */
#include "GlanceOptions.h"
#include "../Asset/ImageAsset.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

class GroupContextStore;
class ImageAssetStore;

// 一张待瞥视的群图片：seq 定位群缓冲里的记录，asset 是已入群库的图片
struct GlanceJob
{
    std::uint64_t groupId = 0;
    std::int64_t seq = 0;
    ImageAsset asset;
    std::string hint;            // 附加提示（如"发送者以表情包形式发送"），可空
    std::int64_t timestamp = 0;  // 消息时间（unix 秒），仅供日志
};

class GlanceService
{
public:
    // 视觉调用 seam：组合根以 models.vision + Dock::RequestVision 适配，测试注入 fake。
    // 返回空串 = 失败/取消（不写回）
    using Vision = std::function<std::string(const ImageAsset &asset, const std::string &hint)>;

    // 队列封顶与去重缓存上限（资源护栏，零旋钮）
    static constexpr std::size_t kMaxQueue = 64;
    static constexpr std::size_t kMaxCacheEntries = 512;
    static constexpr std::size_t kMaxCaptionCodePoints = 80;

    GlanceService(GroupContextStore *store, const ImageAssetStore *assets, GlanceOptions options);
    ~GlanceService();

    GlanceService(const GlanceService &) = delete;
    GlanceService &operator=(const GlanceService &) = delete;

    // 必须在 start() 之前设置；未设置 = 视觉模型未配置，start() 保持 no-op
    void setVision(Vision vision) { this->vision = std::move(vision); }

    // 仅当 enabled && vision && store 才起 worker；否则 LOG_WARNING 说明原因并保持 no-op
    void start();
    bool isRunning() const { return running.load(); }

    // 观察路径调用：锁内入队即返回（不阻塞、不做 IO）；未运行时无副作用
    void enqueue(GlanceJob job);

    // stopping=true → 唤醒 → join；pending 全部丢弃（不写标记）。可重复调用
    void shutdown();

    // 视觉 prompt（固定常量 + 可选 hint 一句）：组合根的 Vision 适配器用它组请求
    static std::string visionPrompt(const std::string &hint);
    // 视觉输出清洗：去首尾空白，换行/|/] 换空格，按 UTF-8 码点截断到 80
    static std::string sanitizeCaption(const std::string &raw);
    // 把 " | <caption>" 插进文本里该资产占位符的 ] 之前；找不到占位符或已有描述返回空串
    static std::string attachCaption(const std::string &text, const std::string &assetId,
                                     const std::string &caption);

private:
    void workerLoop();
    void process(const GlanceJob &job);
    // 每群滚动 60 秒窗口预算：有余量则记一次并返回 true（仅 worker 线程访问）
    bool takeBudget(std::uint64_t groupId);
    void writeCaption(const GlanceJob &job, const std::string &caption);
    void markUnseen(const GlanceJob &job);
    std::string readFingerprintSource(const ImageAsset &asset) const;

    GroupContextStore *const store;
    const ImageAssetStore *const assets;
    const GlanceOptions options;
    Vision vision;

    std::thread worker;
    std::atomic<bool> running{false};
    mutable std::mutex queueMutex;
    std::condition_variable queueChanged;
    std::deque<GlanceJob> queue;      // 待瞥视
    std::deque<GlanceJob> overflowed; // 队列满被挤出的任务：worker 稍后标"未看"
    bool stopping = false;

    // 以下仅 worker 线程访问，无需加锁
    std::map<std::uint64_t, std::deque<std::chrono::steady_clock::time_point>> budgetHistory;
    std::unordered_map<std::uint64_t, std::string> captionCache; // 字节哈希 → 描述
};

#endif // GLANCE_SERVICE_H

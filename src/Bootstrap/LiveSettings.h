#ifndef LIVE_SETTINGS_H
#define LIVE_SETTINGS_H

/*
 * 活值集合（ADR-023，配置实时生效）：每组可在运行时生效的配置一个 Live<T>，
 * 从启动时的 RuntimeSettings 构造。
 *
 * - 组合根的栈对象，声明在所有服务之前、最后析构；拥有者持值，使用者持引用。
 * - 只有 ConfigReloader 经规则表里 applies(函数) 行的应用函数调用成员的 set()。
 * - 业务模块不认识本类型（不得包含 Bootstrap/）：组合根把单个成员以
 *   const Live<T> & 交给模块，模块每次操作开头取一份快照。
 * - 一个键作用于多处时（如 network.proxy），由它的应用函数同时替换几个成员。
 * - 成员的类型都是业务模块自己的 Options：models 的三个专用端点因此各占一个
 *   Live<ModelEndpointOptions>（ModelRuntimeSettings 在 Bootstrap/，业务模块看不到）。
 */
#include "RuntimeSettings.h"
#include "../utils/Live.h"

struct LiveSettings
{
    explicit LiveSettings(const RuntimeSettings &startup);

    Live<ChatOptions> chat;
    Live<ModelEndpointOptions> drawingModel; // models.drawing
    Live<ModelEndpointOptions> visionModel;  // models.vision
    Live<ModelEndpointOptions> workerModel;  // models.worker
    Live<VoiceOptions> voice;
    Live<MemoryOptions> memory;
    Live<WebSearchOptions> webSearch;
    Live<WebFetchOptions> webFetch;
    Live<DockOptions> dock;
    Live<GlanceOptions> glance;
    Live<MessageOptions> message;
};

#endif // LIVE_SETTINGS_H

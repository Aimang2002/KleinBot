#ifndef PUSH_SERVER_H
#define PUSH_SERVER_H

#include "PushDelivery.h"
#include "PushSettings.h"

#include <atomic>
#include <memory>

namespace httplib
{
class Server;
}

// 推送门 HTTP 入口（契约草案，见 docs/design-notes/2026-10-05-推送门与外置能力.md）：
// 外部脚本 POST /push 投递 {target, contents[]}，Bearer 鉴权后交给
// PushDelivery 接缝统一校验（白名单/频率/大小）与投递（润色/出站管线）。
// 门只做协议解析与鉴权，不做业务判断——所有边界规则集中在接缝处
//
// 生命周期模式与 ConfigPanelServer 相同：独立线程 + 200ms 看门狗 +
// 监听失败 10 秒重试；main 退出链在配置面板线程之后 join
class PushServer
{
public:
    // 构建带鉴权与路由的服务器；测试可自行 bind 到临时端口
    static std::unique_ptr<httplib::Server> buildServer(const PushSettings &settings,
                                                        PushDelivery &delivery);

    // main 侧线程入口；令牌为空时拒绝启动（正常路径下加载器已关闭 enabled）
    static void run(PushSettings settings, PushDelivery &delivery,
                    const std::atomic<bool> &running);
};

#endif // PUSH_SERVER_H

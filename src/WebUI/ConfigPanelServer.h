#ifndef CONFIG_PANEL_SERVER_H
#define CONFIG_PANEL_SERVER_H

#include "../Bootstrap/ConfigSnapshotStore.h"
#include "../Bootstrap/RuntimeSettings.h"

#include <atomic>
#include <memory>
#include <string>

namespace httplib
{
class Server;
}

class ConfigPanelServer
{
public:
    // 构建带完整路由与鉴权的面板服务器；测试可自行 bind 到临时端口
    static std::unique_ptr<httplib::Server> buildServer(const WebUiSettings &settings,
                                                        const std::string &configPath,
                                                        ConfigSnapshotStore &store);

    // main 侧线程入口；监听失败每 10 秒重试，running 置假后退出
    static void run(WebUiSettings settings, std::string configPath,
                    ConfigSnapshotStore &store, const std::atomic<bool> &running);
};

#endif

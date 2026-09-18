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

class ModelRegistry;
class GroupListService;
class OneBotApiChannel;

class ConfigPanelServer
{
public:
    // 构建带完整路由与鉴权的面板服务器；测试可自行 bind 到临时端口。
    // models 用于模型注册表保存后的进程内热重载；
    // groups 可空：注入时提供 GET /api/groups 群列表选择器数据（T7c）；
    // apiChannel 可空：注入时提供 GET /api/botinfo 的协议端登录信息查询
    static std::unique_ptr<httplib::Server> buildServer(const WebUiSettings &settings,
                                                        const std::string &configPath,
                                                        ConfigSnapshotStore &store,
                                                        ModelRegistry &models,
                                                        GroupListService *groups = nullptr,
                                                        OneBotApiChannel *apiChannel = nullptr);

    // main 侧线程入口；监听失败每 10 秒重试，running 置假后退出
    static void run(WebUiSettings settings, std::string configPath,
                    ConfigSnapshotStore &store, ModelRegistry &models,
                    GroupListService *groups, const std::atomic<bool> &running,
                    OneBotApiChannel *apiChannel = nullptr);
};

#endif

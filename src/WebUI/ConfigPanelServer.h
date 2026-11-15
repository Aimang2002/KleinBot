#ifndef CONFIG_PANEL_SERVER_H
#define CONFIG_PANEL_SERVER_H

#include "../Bootstrap/ConfigReloader.h"
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
    // store 只读（GET /api/botinfo 取当前快照）；保存配置后的重载只经注入的 reloader
    // （与 #刷新配置文件 同一个入口，ADR-023）；GET /api/config/effects 按产品规则表
    // 返回每个键的生效方式，面板的字段说明由它推出；
    // models 用于模型注册表保存后的进程内热重载；
    // groups 可空：注入时提供 GET /api/groups 群列表选择器数据（T7c）；
    // apiChannel 可空：注入时提供 GET /api/botinfo 的协议端登录信息查询
    static std::unique_ptr<httplib::Server> buildServer(const WebUiSettings &settings,
                                                        const std::string &configPath,
                                                        const ConfigSnapshotStore &store,
                                                        ConfigReloader &reloader,
                                                        ModelRegistry &models,
                                                        GroupListService *groups = nullptr,
                                                        OneBotApiChannel *apiChannel = nullptr);

    // main 侧线程入口；监听失败每 10 秒重试，running 置假后退出
    static void run(WebUiSettings settings, std::string configPath,
                    const ConfigSnapshotStore &store, ConfigReloader &reloader,
                    ModelRegistry &models, GroupListService *groups,
                    const std::atomic<bool> &running, OneBotApiChannel *apiChannel = nullptr);
};

#endif

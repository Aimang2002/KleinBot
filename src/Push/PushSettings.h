#ifndef PUSH_SETTINGS_H
#define PUSH_SETTINGS_H

#include <cstdint>
#include <string>
#include <vector>

// 推送门（Push Door）模块自有配置：外部脚本经 HTTP POST 投递
// {目标, 类型化内容块}，Klein 以自身出站管线投递。
// 契约状态为草案（见 AGENTS.md 窄腰模型一节）；阶段 2 的 MCP 门
// （poll_due_tasks 约定 + tools/call）将复用同一接缝，本结构只描述门
// 自身的部署参数，与内容契约的形态无关。
struct PushSettings
{
    bool enabled = false;
    std::string bind = "127.0.0.1";
    // 55347：Web 面板 55346 的下一号，两个本地 HTTP 服务相邻可记；
    // 配置缺省值的唯一权威在 SchemaConfig.h 的 kDefaultPushPort，
    // 此处默认值仅作结构兜底（Bootstrap 映射总是显式赋值）
    int port = 55347;
    std::string accessToken;
    // 目标白名单：群号 / QQ 号。空 = 不放行任何目标（fail-closed，
    // 加载器会在 enabled 且两列表皆空时直接关闭推送门）
    std::vector<std::int64_t> allowedGroups;
    std::vector<std::int64_t> allowedUsers;
    // 频率帽：按目标每小时计，限制单一外部来源刷屏的事故半径
    int maxPerHour = 60;
};

#endif // PUSH_SETTINGS_H

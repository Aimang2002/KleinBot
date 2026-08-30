<div align="center">

<img src="assets/avatar-rounded.png" width="180" height="180" alt="KleinBot" />

# KleinBot

**基于 C++17 的自托管多模态 QQ Agent**

*KleinBot 是一个以 QQ 为交互载体、在应用层实现多模态感知、工具调用与任务编排的对话式 Agent*

[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)](#快速开始) [![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20Windows-lightgrey)](#快速开始) [![OneBot](https://img.shields.io/badge/protocol-OneBot%2011-green)](#简介) [![Agent](https://img.shields.io/badge/Agent-tool%20calling-purple)](#核心特性) [![Multimodal](https://img.shields.io/badge/multimodal-application%20layer-orange)](#核心特性) [![License](https://img.shields.io/badge/license-MIT-yellow)](#开源协议)

</div>

<p align="center"><a href="README.md">中文</a> | <a href="README.en-US.md">English</a></p>

---

## 目录

- [简介](#简介)
- [核心特性](#核心特性)
- [快速开始](#快速开始)
- [配置](#配置)
- [模型接入](#模型接入)
- [内置命令](#内置命令)
- [模型工具](#模型工具function-calling)
- [长期记忆](#长期记忆)
- [运行时架构](#运行时架构)
- [运行资源目录](#运行资源目录)
- [开发与测试](#开发与测试)
- [鸣谢](#鸣谢)
- [开源协议](#开源协议)

## 简介

KleinBot（下文简称 **Klein**）是一个使用 C++17 开发、以 QQ 为交互载体的应用层多模态 Agent。它通过 OneBot 接入 QQ，统一编排文本、图片、语音和音乐等消息，并组合不同厂商的 LLM、视觉、绘图与语音能力。

模型可以根据自然语言自主选择搜索、网页阅读、图片处理、长期记忆和提醒等工具；Klein 负责上下文、工具执行、结果回灌、权限和资源边界，将模型推理组织成可控的任务闭环。

> Klein 不实现协议端本身，而是对接支持 CQ 码的第三方 OneBot 实现。测试使用的协议端为 [LLOneBot](https://github.com/LLOneBot/LLOneBot)，其他兼容 OneBot 11 的实现原则上也可以对接。

## 核心特性

| 能力 | 说明 |
| --- | --- |
| **Agent Loop** | 模型自主决定调用哪些工具、调用几轮，直到给出回答；应用层负责工具执行、结果回灌和边界控制；对产生畸形参数的第三方网关做了兼容 |
| **联网能力** | `klein_web_search` 关键词检索（Tavily，时间策略由模型参数表达）+ `klein_web_fetch` 抓取指定链接正文（超长内容自动按问题摘要） |
| **长期记忆** | 无向量检索的用户资料、偏好与事实记忆，后台自动提取、按需召回，`#重置上下文` 一并清除 |
| **Web 配置面板** | 浏览器中表单化编辑全部配置：Klein 主题控制台界面、密钥掩码显示、保存即校验并原子写回、按影响等级报告变更 |
| **多模态** | 支持视觉的模型直接读图，其余模型自动走独立视觉模型；图片按用户隔离存储为可复用资产 |
| **语音回复** | 对接 [GPT-SoVITS](https://github.com/RVC-Boss/GPT-SoVITS) 的文字转语音 |
| **定时提醒** | 自然语言注册（模型解析为结构化时间）、SQLite 持久化、每天/每周重复，重启后 24 小时内漏触发的提醒会补发 |
| **消息调度** | 同一用户消息严格串行、不同用户并行，队列满时平滑拒绝 |
| **多平台** | Linux / Windows（MinGW-w64）双平台构建，Windows 产物零外部 DLL 依赖 |

## 快速开始

### 环境要求

- **CMake ≥ 3.21**，以及支持 C++17 的编译器
- **Linux**：GCC + 系统 Boost、curl、SQLite（通过 `find_package` 解析）
- **Windows**：带 **POSIX 线程模型**的 MinGW-w64（推荐 MSYS2 UCRT64/MINGW64），`gcc`、`g++`、`mingw32-make` 在 `PATH` 中。依赖自动获取：仓库内最小 Boost.Asio/Beast 头文件、固定版本的 SQLite 与 curl（Schannel，无需 OpenSSL 和 CA 证书）

### Linux

```bash
git clone https://github.com/Aimang2002/KleinBot.git
cd KleinBot
cmake --preset linux-release
cmake --build --preset linux-release
```

产物：`build/linux-release/KleinBot`（文件名不含版本号，版本与构建信息用 `--version` 查看）

### Windows

```bash
git clone https://github.com/Aimang2002/KleinBot.git
cd KleinBot
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release
```

产物：`build/windows-mingw-release/KleinBot.exe`（默认静态链接 MinGW 运行时，仅依赖 Windows 系统 DLL）

> 若 CMake 报错 `POSIX thread support`，说明当前 MinGW 使用 win32 线程模型，请更换为带 POSIX 线程支持的工具链。

### 运行

1. 首次运行：工作目录下没有 `.config.json` 时会自动生成一份占位骨架（Web 面板同时启用，访问令牌打印在启动日志中），也可以手动复制 `config.example.json` 为 `.config.json`（点前缀隐藏文件；从旧版本升级请把原 `config.json` 改名，或用 `--config` 指定旧路径）；
2. 准备一个 OneBot 协议端（如 [LLOneBot](https://github.com/LLOneBot/LLOneBot)）并建立通信；
3. 运行可执行文件，向机器人私聊或群内 @ 它即可对话。

查看构建信息（版本号、git 提交、构建时间、构建类型、编译器、libc 版本、架构）：

```bash
./KleinBot --version   # 或简写 -V
```

## 配置

KleinBot 使用 Schema 化 JSON 配置，运行时读取当前工作目录的 `.config.json`，仓库中的 `config.example.json` 是不含密钥的完整模板。根节点按职责划分：

| 节点 | 作用 |
| --- | --- |
| `schema_version` | 配置结构版本，当前必须为 `1` |
| `bot` | Bot ID、管理员、名称和群聊策略 |
| `chat` | 默认模型、采样参数、消息限制和 OneBot action 名称 |
| `models` | 模型注册文件、绘图和视觉配置 |
| `voice` | TTS 开关、服务地址和参考音频（音频写入系统临时目录） |
| `features` | 可选业务功能开关 |
| `memory` | 长期记忆模型、批次和召回限制 |
| `web_search` | Tavily 联网搜索、超时和单次结果裁剪配置 |
| `web_fetch` | 网页抓取工具的正文上限、超时和缓存配置 |
| `storage` | SQLite 与图片资源目录 |
| `network` | 公共代理配置 |
| `communication` | 协议、活动传输 Profile 和网络默认值 |
| `webui` | Web 配置面板开关、绑定地址、端口和访问令牌 |

### 密钥管理

Secret 字段统一支持本地字面量或环境变量两种来源，推荐后者：

```json
{"literal": "local-token"}
```

```json
{"from_env": "KLEIN_ONEBOT_TOKEN"}
```

### 联网搜索（默认关闭）

通过环境变量提供 Tavily Key 后启用：

```bash
export KLEIN_TAVILY_API_KEY="tvly-..."
```

```json
"web_search": {
    "enabled": true,
    "provider": "tavily",
    "api_key": {"from_env": "KLEIN_TAVILY_API_KEY"},
    "search_depth": "basic",
    "max_results": 5,
    "max_content_chars": 2000
}
```

搜索完全由模型自主决策，没有关键词强制触发。搜索证据只注入当前工具轮次，不写入对话历史和长期记忆；模型侧函数名为 `klein_web_search`（避免部分网关抢占保留名 `web_search`）。

### 网页抓取（默认关闭）

`klein_web_fetch` 无需第三方 API Key：

```json
"web_fetch": {
    "enabled": true,
    "max_content_chars": 12000,
    "cache_ttl_seconds": 900
}
```

抓取流程：本机 curl 下载（大小上限、超时与取消保护）→ 提取标题与正文（丢弃 script/style/nav 等噪声）→ 超过 `max_content_chars` 时把「用户问题 + 正文」交给默认模型做原文摘录，蒸馏失败退回 UTF-8 安全截断。结果按 URL 缓存 15 分钟；只允许公网 http/https 地址，本地回环、内网段和重定向到内网的目标会被拒绝。

### 通信配置

使用命名 Profile，通过 `active_transport` 互斥选择一种传输（正向 WebSocket / 反向 WebSocket / HTTP）。一次运行只启用一种完整通信模式；两种 WebSocket 模式都使用单条 Universal 连接同时接收事件和发送动作，`http` 模式组合 OneBot HTTP API 与 HTTP 事件上报：

```json
"communication": {
    "protocol": {"type": "onebot", "options": {}},
    "active_transport": "onebot-http",
    "transports": {
        "onebot-http": {
            "type": "http",
            "api": {
                "base_url": "http://127.0.0.1:3000",
                "access_token": {"from_env": "KLEIN_ONEBOT_API_TOKEN"}
            },
            "events": {
                "bind": "127.0.0.1",
                "port": 8080,
                "path": "/onebot/events",
                "secret": {"from_env": "KLEIN_ONEBOT_EVENT_SECRET"}
            }
        }
    }
}
```

HTTP 的两个方向使用不同认证语义：Klein 调用 OneBot API 时通过 `api.access_token` 发送 `Authorization: Bearer`；协议端上报事件时通过 `events.secret` 对原始请求体计算 HMAC-SHA1 签名（`X-Signature: sha1=<hex>`）。旧版 `events.access_token` 仍兼容。

### 配置校验与热刷新

配置加载统一完成类型转换、默认值填充和语义校验：可选字段损坏时使用安全默认值或关闭对应功能；核心身份、活动协议和活动传输无效时拒绝启动。业务模块不读取配置文件，也不依赖全局配置对象。

`#刷新配置文件` 会重新读取同路径、完整校验候选配置、生成动态/需重建/需重启差异并原子发布新快照；候选无效时保留旧快照。

### Web 配置面板（可选）

在浏览器中编辑 `.config.json`，替代手工改文件：

```json
"webui": {
    "enabled": true,
    "bind": "127.0.0.1",
    "port": 55346,
    "access_token": {"from_env": "KLEIN_WEBUI_TOKEN"}
}
```

```bash
export KLEIN_WEBUI_TOKEN="自定一个强令牌"
```

启动后访问 `http://127.0.0.1:55346/`，首次打开输入访问令牌即可（输错会有即时提示）。面板为侧边导航的控制台布局，深色 Klein 主题，左侧按配置模块分区导航，右侧编辑对应分区。面板行为：

- 页面文件 `panel.html` 位于可执行文件旁（构建时自动从 `src/WebUI/panel.html` 同步），改动后刷新浏览器即可生效；文件缺失时 Web 面板不会启动（启动日志报 error 级提示），补回文件后重启即可恢复；
- 表单化编辑全部配置节（含未知字段），保存时先跑与启动一致的完整校验，不合法直接拒绝并逐字段提示，不会写坏文件；
- 字段标签和分区标题悬停会显示用途说明，包括取值范围与生效方式（保存即生效 / 需重启）；
- “模型供应商”页管理 `source/ModelsName.json`：添加/删除供应商、批量填写模型名称、选择 API 标准，注册表保存后需重启机器人加载；
- 密钥字段（`api_key` / `access_token` / `secret`）以掩码显示，浏览器永远拿不到明文；留空表示保持原值，`from_env` 引用原样保留；
- 保存经临时文件原子替换并收紧文件权限为 0600，随后自动刷新内存快照，并报告各变更的影响等级（动态 / 需重建 / 需重启）——需重启的项重启机器人后才生效。

首次运行没有 `.config.json` 时会自动生成占位骨架配置，Web 面板随之启用：绑定 `127.0.0.1`、端口 55346，访问令牌随机生成并打印在启动日志中，按日志提示在面板里补全机器人 QQ、默认模型和通信配置后重启即可。

安全边界：`enabled: true` 但令牌缺失时面板自动关闭；默认只绑定回环地址，绑定非回环地址会产生安全警告。如需远程访问，请由反向代理提供 TLS 后再暴露，不要直接把端口暴露到公网。

## 模型接入

模型在 `source/Model/ModelsName.json` 中按 API 分组注册（路径固定，不可配置）。同组内所有模型共享该组的 `api_key` 与 `api_endpoint`：

```json
{
    "Models": [
        {
            "ModelName": ["gpt-4o", "gpt-4"],
            "api_key": "sk-xxx",
            "api_endpoint": "https://api.xxx.com/v1/chat/completions",
            "APIStandard": "OpenAI",
            "Capabilities": { "vision": true }
        },
        {
            "ModelName": ["deepseek-chat", "deepseek-reasoner"],
            "api_key": "sk-yyy",
            "api_endpoint": "https://api.yyy.com/v1/chat/completions",
            "APIStandard": "OpenAI"
        }
    ]
}
```

- `Capabilities.vision` 可选，默认 `false`。开启后当前消息中的图片会直接作为多模态内容发给该模型；未声明视觉能力的模型继续走独立视觉模型和 `inspect_image` 工具。若接口以明确 4xx 声明只接受文本，本次会自动降级到工具路径；超时、鉴权和服务不可用不触发降级。
- 兼容 OpenAI API 规范的本地推理服务或第三方中转网关，均可通过 `OpenAI` APIStandard 接入。

| API 标准 | 支持 | 说明 |
| --- | :---: | --- |
| OpenAI | ✅ | 全功能：工具调用、联网、图片生成 |
| Anthropic | ✅ | 文本聊天、视觉调用与工具调用（原生 tool_use 协议）；不支持图像生成 |
| Google | ❌ | 暂不实现 |
| Azure | ❌ | 暂不实现 |

## 内置命令

所有命令以 `#` 开头。带参数的命令使用 `:` 分隔，如 `#切换模型:gpt-4o`。

### 公有命令

| 命令 | 说明 | 示例 |
| --- | --- | --- |
| `#帮助` | 向发送者介绍操作命令 | `#帮助` |
| `#重置对话` | 清空当前对话上下文、换个话题重新开始；历史记录与长期记忆保留，旧话题仍可召回 | `#重置对话` |
| `#重置上下文` | 彻底删除该用户全部对话历史、长期记忆与图片资源 | `#重置上下文` |
| `#删除上条对话` | 回退最近一轮对话（别名 `#rewind` / `#undo`） | `#删除上条对话` |
| `#设置人格` | 设置自定义人格描述，持久化到 SQLite，重启后保留 | `#设置人格:你是一个傲娇猫娘` |
| `#人格还原` | 清除自定义人格，恢复 `source/soul.md` 默认人格 | `#人格还原` |
| `#切换模型` | 切换到已注册的模型 | `#切换模型:deepseek-chat` |
| `#查询当前模型` | 查询当前使用的模型 | `#查询当前模型` |
| `#模型列表` | 列出所有已注册模型 | `#模型列表` |
| `#搜歌` | 搜索网易云音乐并返回卡片（目前仅支持网易云） | `#搜歌:rubia` |
| `#图片生成` | 按描述生成并发送图片（别名 `#生成图片`） | `#图片生成:赛博朋克城市夜景` |
| `#开启语音` / `#关闭语音` | 开关当前用户的语音回复 | `#开启语音` |

其中部分公有功能也支持自然语言触发，不要求使用 `#` 命令前缀。模型会根据语义调用对应工具：

| 功能 | 自然语言示例 |
| --- | --- |
| 查询当前模型 | 「你现在使用的是什么模型？」 |
| 开关语音回复 | 「请用语音回复我」「关闭语音」 |
| 图片生成 | 「帮我画一张赛博朋克城市夜景」 |
| 设置、查询或取消提醒 | 「明天早上 9 点提醒我开会」「我有哪些提醒」「取消编号 1 的提醒」 |

自然语言触发由当前模型判断意图并调用工具，可能受模型能力和相关功能配置影响；`#重置对话`、`#重置上下文`、`#设置人格`、`#人格还原`、`#切换模型` 等命令仍需使用显式命令格式。

### 管理员命令

仅管理员（`bot.manager_id`）可用，普通用户发送会被忽略或提示权限不足。

| 命令 | 说明 |
| --- | --- |
| `#激活语音` / `#冻结语音` | 全局允许 / 禁止语音回复 |
| `#刷新配置文件` | 重新校验并加载配置（见[配置校验与热刷新](#配置校验与热刷新)） |
| `#获取服务器inet4` / `#获取服务器inet6` | 获取本机所有 IPv4 / IPv6 地址 |
| `#获取服务器公网IP` | 获取本机公网 IP |

### 图片消息

发送图片不需要任何命令。用户消息附带图片时，若当前主模型声明了视觉能力，图片直接作为多模态内容交给模型；否则 Klein 会把图片存为用户资产，模型通过 `inspect_image` 工具查看。之后询问「这张图 / 刚才那张图」的内容、要求重新发送或重新生成，都会走对应的图片工具。

## 模型工具（Function Calling）

对话过程中模型可自主调用以下工具，无需用户指令触发。工具调用使用标准 OpenAI tools 协议；`Anthropic` APIStandard 的模型走原生 tool_use 协议，由适配器自动完成双向转换。

| 工具 | 作用 | 典型场景 |
| --- | --- | --- |
| `klein_web_search` | Tavily 联网搜索，支持 topic / time_range / days 时间参数 | 「最新的 XX」「今天的新闻」 |
| `klein_web_fetch` | 抓取指定网址并提取正文，超长内容自动按问题摘要 | 用户发出链接要求阅读 |
| `inspect_image` | 查看当前或历史图片内容 | 「这图片里是什么」 |
| `generate_image` | 生成图片并直接发送 | 「帮我画一张 XX」 |
| `send_image` | 重新发送历史图片 | 「把刚才那张图再发一次」 |
| `recall_conversation` | 检索长期记忆与原始历史 | 「我以前跟你说过什么」 |
| `set_reminder` | 注册定时提醒（支持每天/每周重复） | 「明天早上9点提醒我开会」 |
| `list_reminders` | 列出当前用户的待触发提醒 | 「我有哪些提醒」 |
| `cancel_reminder` | 取消指定编号的提醒 | 「把提醒取消了」 |
| `get_current_model` | 查询当前使用的模型 | 「你现在是什么模型」 |
| `set_voice_mode` | 开关当前用户的语音回复 | 「用语音回复我」 |
| `admin_control` | 管理员控制（语音开关、刷新配置、服务器网络查询） | 管理员命令对应 |
| `get_time` | 获取当前时间 | 时间相关问题 |

工具循环为标准 agent loop：模型请求工具 → Klein 执行并把结果回灌 → 模型继续，直到给出文本回答或达到轮次上限（达到上限时基于已收集的证据收尾作答）。联网相关两个工具默认关闭，启用方式见[配置](#配置)。

## 长期记忆

Klein 完整保存原始对话，并在后台从多轮对话中提取用户资料、偏好、关系、事件、状态、决定、任务和技术事实，使用稳定的 `memory_key` 更新同一事实。

**召回不依赖 Embedding**：对当前问题或模型提供的检索短语做 ASCII 归一化与中文二元/三元短语扩展，同时检索长期记忆和原始历史，按文本相关性、重要程度、置信度和更新时间统一排序。长期记忆命中后不阻止原始历史参与召回，重复来源消息会被过滤。

对于用户偏好、人物资料等明确属性，提取器还会写入开放的**实体—属性—值事实层**：保留当前值与历史版本，支持 `current` / `earliest` / `previous` / `timeline` 四种时间查询；删除最近上下文时自动恢复仍有有效来源的上一版本。

主模型回答前，系统用规则识别「以前说过」「最早」「上一版」等召回和时间意图，再规划结构化查询，不额外调用一次模型。记忆提取在后台线程进行，不阻塞聊天回复。`#重置上下文` 会同时清除该用户的原始历史和长期记忆；`#重置对话` 只清空当前上下文窗口，历史与记忆保留、仍可召回。

## 运行时架构

- **Agent 运行时**：`ChatService` 负责一次任务的运行时编排，`ToolRegistry` 暴露可用工具，`ToolContext` 传递用户、消息和会话上下文；这一层也承担 Harness 的职责：模型负责意图判断与工具选择，应用层负责执行边界与结果整合。
- **Agent Loop**：模型请求工具 → Klein 校验权限并执行 → 工具结果与出站消息回灌 → 模型继续规划，直到返回最终回答、工具产生终止消息或达到轮次上限。
- **消息调度**：按 `user_id` 分片的 `KeyedTaskScheduler`——同一用户严格串行，不同用户并行。未配置 `chat.worker_threads` 时初始线程数为 CPU 逻辑核心数、最大 4 倍，空闲自动缩减。
- **背压**：待处理消息最多 1024 条（程序内部定值），达到上限时拒绝新消息并返回繁忙提示。
- **优雅退出**：所有线程共享运行状态，收到 `SIGINT` / `SIGTERM` 后停止重连、等待任务结束并依次回收资源；日志线程退出前排空日志队列。
- **定时提醒**：后台线程每 3 秒轮询到期提醒，经模型渲染后私聊送达（模型失败时兜底直发原文）；提醒持久化于 SQLite（`reminders` 表），重启后 24 小时内漏触发的补发、超窗的滚动到下一轮或丢弃。
- **图片资产**：聊天图片按用户隔离存储，上下文中以 `asset_id` 占位；`#重置上下文` 会同步清理关联图片文件。

## 运行资源目录

可执行文件工作目录下的 `source/`：

| 目录 | 作用 |
| --- | --- |
| `Model/` | 模型注册文件 `ModelsName.json`（路径固定，见[模型接入](#模型接入)） |
| `soul.md` | 默认人格文本；未用 `#设置人格` 设置自定义人格的用户读取此文件，文件缺失时使用内置英文兜底 |
| `image_assets/` | 聊天图片资产（按用户隔离，`#重置上下文` 时同步清理） |
| `.conversations.db` | 会话、记忆、提醒、图片资产元数据的 SQLite 数据库（点前缀隐藏，改名前请先停止机器人） |

`#帮助` 的文本硬编码于 `src/Command/HelpText.h` 并编译进可执行文件（内容定稿后不常改，调整时只改该文件）；TTS 生成的语音写入系统临时目录（Linux `/tmp/kleinbot/`、Windows `%TEMP%\kleinbot\`），发送成功后自动删除。

## 开发与测试

```bash
# Linux Debug + 测试
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

测试基于 GoogleTest / CTest，覆盖 Action、Tool 适配器、命令映射，以及 SQLite 会话与图片资产生命周期的集成测试。自动化测试不调用真实模型 API，统一使用 Fake/Mock 适配器与临时数据库。Windows 对应 `windows-mingw-debug` 预设。

第三方依赖：[curl](https://github.com/curl/curl)、[Boost](https://github.com/boostorg/boost)（仅最小 Asio/Beast 头文件，随仓库提供）、[SQLite](https://www.sqlite.org/)、[nlohmann/json](https://github.com/nlohmann/json)（随仓库提供）、[cpp-httplib](https://github.com/yhirose/cpp-httplib)（v0.53.1 单头文件，随仓库提供，用于 Web 配置面板）、[GoogleTest](https://github.com/google/googletest)（仅测试构建）。Windows 构建会自动下载固定版本的 curl 和 SQLite 并校验哈希。

> 注意：本地 `build/.config.json`、API Key、Token、QQ ID、数据库和生成媒体不得提交到仓库。

## 鸣谢

- [nlohmann/json](https://github.com/nlohmann/json)
- [cpp-httplib](https://github.com/yhirose/cpp-httplib)
- [Boost](https://github.com/boostorg/boost)
- [curl](https://curl.se/)
- [SQLite](https://www.sqlite.org/)
- [GoogleTest](https://github.com/google/googletest)

## 开源协议

本项目以 [MIT License](LICENSE) 开源。随仓库分发的第三方依赖（nlohmann/json、Boost 头文件、cpp-httplib 等）保留其原始许可证；发布包须附带第三方许可声明，且不得包含用户数据与密钥。

---

<div align="center">

**KleinBot** · Maintained by [Aimang2002](https://github.com/Aimang2002)

</div>

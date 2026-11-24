# Repository Guidelines

## Project Structure & Module Organization

KleinBot is a C++17 QQ bot built with CMake. Core source lives in `src/`, grouped by responsibility: `Action/` for reusable business actions, `Command/` for explicit command adapters and dispatch, `Tool/` for model-callable tools, `ChatService/` for chat/tool orchestration, `Asset/` for image resources, `Persistence/` and `Memory/` for SQLite-backed state, `Network/` for WebSocket clients, and `ModelApiCaller/` for model/image/voice adapters. Runtime resources and secrets live under `build/` and must not be committed.

`Library/` contains deliberately vendored dependencies only. `nlohmann/json.hpp` is included directly. `Library/boost/` contains the pinned minimal Boost.Asio/Beast header archive and license; do not replace it with a full Boost source tree.

## Build, Test, and Development Commands

Prefer committed CMake Presets rather than developer-specific paths.

Linux Debug and tests:

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

Linux Release:

```bash
cmake --preset linux-release
cmake --build --preset linux-release
```

Windows MinGW Release:

```bash
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release
```

Windows requires MinGW-w64 with POSIX thread support. The Windows build defaults to bundled dependencies: pinned Boost headers from `Library/boost/`, downloaded SQLite amalgamation, and statically built curl using Schannel. The MinGW runtime is statically linked by default. Linux defaults to system Boost, curl, and SQLite through `find_package()`.

`CMakeUserPresets.json` is optional local configuration and must not be committed or required for a normal build. Never add developer-machine absolute dependency paths to `CMakeLists.txt`.

Sources use `GLOB_RECURSE CONFIGURE_DEPENDS`; CMake checks for added or removed `.cpp` files during builds.

## Coding Style & Naming Conventions

Follow the existing C++ style: 4-space indentation in newer files, braces on their own line for functions/classes, PascalCase class names, and local naming consistent with nearby code. Keep headers paired with `.cpp` files inside the same module directory. Prefer `nlohmann::json` objects over manual JSON strings. New reusable operations should be implemented as Actions, then exposed through Command and/or Tool adapters only when required.

## Testing Guidelines

GoogleTest and CTest are available under `tests/`. Unit tests cover Actions, Tool adapters, and Command mappings; integration tests cover SQLite conversation and image-asset lifecycles. Add focused tests for behavior changes when a public seam already exists. Only adjust production dependencies when testing difficulty reveals a real coupling problem, such as an internally constructed network client or mandatory global singleton.

Run the narrowest relevant tests first, then `ctest --preset linux-debug`. Do not call real model APIs in automated tests; use Fake/Mock adapters and temporary databases/directories. Real QQ/model verification remains a manual smoke test.

## Dependency and Build Rules

- Keep Windows and Linux dependency strategies behind CMake targets; production code must not know where libraries came from.
- Pin automatically downloaded dependency versions and verify archive hashes.
- Keep bundled dependencies minimal; do not vendor full Boost, OpenSSL, or platform-specific prebuilt library trees without an explicit design decision.
- MinGW and MSVC binaries are ABI/toolchain-specific and must not share compiled third-party artifacts.
- Update `README.md`, `CMakePresets.json`, and `docs/technical-design.md` whenever supported build commands, dependency versions, runtime DLL requirements, or packaging behavior changes.

## Documentation and Milestone Sync

`docs/technical-design.md` is the source of truth for the currently adopted architecture, build chain, persistence behavior, and packaging plan. At the end of a meaningful development phase—or whenever the user asks for a阶段性总结—review and update it before final handoff.

Documentation rules:

- Record implemented behavior as current fact.
- Mark planned, partial, experimental, and unsupported behavior explicitly.
- Update build chain, dependency acquisition, release packaging, resource lifecycle, context persistence, permissions, and test coverage when those areas change.
- Remove or rewrite statements made obsolete by the implementation; do not only append new notes.
- Keep `README.md` focused on user-facing setup and build commands; keep design rationale and invariants in `docs/technical-design.md`.
- Keep `docs/tech-max/` (per-volume walkthrough docs) in sync with `docs/technical-design.md`: whenever a change touches behavior described in a tech-max volume, update that volume in the same change, including indirect details such as counts, enum lists, cross-volume references, and end-to-end walkthrough examples.
- `AGENTS.md`, `docs/technical-design.md` and `docs/tech-max/` are tracked and public (since 2026-11-24). Everything else under `docs/` and `CLAUDE.md` are local working notes and stay Git-ignored; do not force-add them. Before committing anything under `docs/`, check it for secrets, real QQ/group IDs, chat excerpts and machine-specific paths.

### 关联点必须一次改全（2026-09-15 项目规则，起因：perception 节漏进配置骨架/示例致面板分区不渲染）

改动不能只改"手头这一处"。任何被多个文件描述的事实（配置键、消息字段、表、线程、计数、枚举、模块清单）都有若干**关联点**，必须同一次改动内全部更新，否则就会出现"代码能用但面板看不见/用户照抄示例收到未知字段警告/文档与代码不符"的半成品状态。

做法：动工前先 grep 该标识符在全仓的出现，列出关联点清单再逐项改完；提交前复查清单。

已知的高频关联点（详见 `docs/tech-max/20-扩展开发指南.md` §6 与 §9）：

- **加/改配置键**：`SchemaConfig.h`、`ConfigLoader.cpp`（两级 unknownFields 白名单 + 校验）、**`ConfigTemplate.cpp`（首跑骨架）**、**`config.example.json`**、`RuntimeSettings` 映射、规则表 `Bootstrap/ConfigRules.cpp` 加一行并回答是否实时生效（`applies(应用函数)` 或 `restart()`，2026-11-15 起取代改 `ConfigDiff.cpp`）、模块 Options + 组合根装配、`panel.html`（专用交互则接 `collectConfig`）、测试（含 `ConfigRulesTests` 的键数断言）、卷04 + README。
- **加/改入站字段**：`Port/InboundMessage.h`、`OneBotEventDecoder`、卷05 字段提取表、卷06 字段表、消费方所在卷。
- **加 SQLite 表**：Store 构造函数 DDL、卷13 表汇总与生命周期总表、集成测试。
- **加线程/调度点**：`main.cpp` 生命周期、卷22 线程表、technical-design §4.2、退出链。
- **加模块/源文件**：卷01 目录地图与体量参考、`architecture_dependency_test.py` 依赖约束、测试文件清单（卷21）。
- **计数值/枚举变化**：全仓 grep 该数字或枚举名（卷内多处引用，如"全库 N 张表""M 个测试文件"）。

自检命令：`cp config.example.json /tmp/check.json && ./build/KleinBot_debug --config /tmp/check.json --check-config`（未知字段告警须为 0）。

### 窄腰模型与接口生命周期（2026-10-05 项目规则）

对外接口与模块边界遵循沙漏/窄腰（narrow waist）模型：腰（契约）越窄越稳定，腰两侧（上层用法、下层实现与插件）越多样。后续插件化方向（推送门、MCP 门、外部脚本）一律以此裁决冲突。三条纪律：

1. **稳定压倒聪明**：已定稿的契约默认不动；扩展必须版本化（新增类型/端点，旧路径保留一个版本周期），并同步 `supported` 类能力声明，绝不静默变更语义。
2. **拒绝与接受同等重要**：接口的非目标（外部不主动、消息流/内部状态不出进程、只发消息不改行为）是承重墙；放宽任何一条视为架构变更，必须新增 ADR 并同步 technical-design。
3. **语法与语义分离**：契约只定义「怎么说」（Target + 类型化 ContentBlocks + 边界规则），不得编码「说什么/为什么说」——具体功能语义住在消费者/插件侧。禁止为单一用途在通用契约中增加专用字段。

接口生命周期与兼容承诺：

- **草案**：新契约默认为草案，可自由修改乃至推倒；但每次推倒必须产出 ADR（记录旧方案为何失效、新方案假设什么）与防回归测试，否则同类推翻会循环发生。
- **转正**：至少存在 2 个真实独立消费者，且经历过一次未破坏契约的扩展，契约才算标准。
- **冻结**：只有存在外部用户/部署者真实依赖时才承诺向下兼容；无消费者期间不预先承诺兼容——兼容是给消费者的礼物，不是给自己的束缚。

规则落地优先级：能机检的不停留在文档（测试/编译期 static_assert/`architecture_dependency_test.py` 同款）；能挂外部标准的不自造（OneBot/MCP/HTTP，继承其稳定性）；必须自造的把表面积压到最小——接口表面积与 churn 概率成正比。

## Commit & Pull Request Guidelines

Use conventional prefixes such as `feat:`, `fix:`, `refactor:`, `test:`, `docs:`, and `chore:` followed by a short Chinese description. Before committing, run relevant builds/tests and `git diff --check`. **Do not commit automatically after a change; wait for explicit user confirmation before running `git commit`.**

迭代工作流（项目规则，2026-09-05；2026-10-25 修订）：功能开发在特性分支上进行；每个工作项完成后准备独立提交作为登记，但**每次修改完成后不要自动提交到本地，必须等待用户明确确认后再执行 commit**；每次提交前必须完成相关测试与构建验证。**推送到任何远程（origin/cnb）必须先取得用户明确批准，未经批准绝不 push**；推送时同步所有相关分支。

## Security & Configuration Tips

Do not commit API keys, QQ IDs, certificates, generated media, SQLite databases, local `build/config.json`, `CMakeUserPresets.json`, or runtime logs. Release packages must include third-party license notices and must not include user data or secrets.

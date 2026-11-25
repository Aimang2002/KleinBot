# 参与贡献

感谢你愿意为 KleinBot 出力。这份文档说明怎么搭环境、怎么提改动、改动要满足什么要求。完整的工作规范在 [AGENTS.md](AGENTS.md)（人和 AI 编码代理共用同一份），本文是它的入门版；两者不一致时以 `AGENTS.md` 为准。

## 先读什么

| 想了解 | 看这里 |
| --- | --- |
| 怎么安装、配置、运行 | [README.md](README.md) |
| 整体架构、设计决策（ADR）、不变量 | [docs/technical-design.md](docs/technical-design.md) |
| 某个模块的实现细节 | [docs/tech-max/](docs/tech-max/README.md)，按卷查 |
| 新功能该放在哪 | [docs/tech-max/20-扩展开发指南.md](docs/tech-max/20-扩展开发指南.md) |

## 搭环境与跑测试

需要 C++17 编译器（GCC 11 及以上）、CMake 3.21 及以上、Python 3。Ubuntu 22.04 上的依赖安装（与 CI 用的是同一份列表）：

```bash
sudo apt-get install -y g++ make cmake git \
  libboost-dev libcurl4-openssl-dev libsqlite3-dev \
  libgtest-dev python3 tzdata
```

构建并跑全部测试：

```bash
cmake --preset linux-debug
cmake --build --preset linux-debug
ctest --preset linux-debug
```

只跑一组测试：

```bash
./build/linux-debug/tests/kleinbot_tests --gtest_filter='MemoryRecall*'
```

Windows（MinGW-w64）与发布构建的命令见 `AGENTS.md`。自动化测试不调用真实的模型 API，也不连真实的 QQ 协议端：用 Fake/Mock 适配器和临时数据库。有两项集成测试只在设置了相应环境变量时才运行，平时显示为跳过，属正常。

## 分支与 PR

- `main` 是已发布的版本；日常开发在版本分支上（当前是 `2.4.2`）。
- 从版本分支拉出自己的分支，命名 `feat/v<版本>-<主题>`、`fix/v<版本>-<主题>`、`docs/…`、`chore/…`。
- PR 的目标分支是版本分支，不是 `main`。
- 一个 PR 只做一件事。顺手发现的别的问题，另开 PR 或 issue。
- 提 PR 前在本地跑通 `ctest --preset linux-debug` 和 `git diff --check`；CI 会再跑一遍。

## 改动要满足什么

**关联点一次改全。** 同一个事实常常写在好几个文件里（配置键、消息字段、数据库表、线程、模块清单、各种计数）。只改手头那一处，会留下“代码能跑，但面板看不见 / 示例配置报未知字段 / 文档和代码对不上”的半成品。动手前先全仓搜索这个标识符，列出要改的地方再逐项改完。常见的几类关联点和各自的清单在 `AGENTS.md` 的“关联点必须一次改全”一节，PR 模板里也有一份自查表。

**守住依赖方向。** 例如业务模块不能直接读配置文件，SQL 只能出现在各个 Store 里，OneBot 协议的概念只能出现在 `Network/` 与 `Protocol/`。其中一部分由 `tests/architecture_dependency_test.py` 自动检查，完整的规则在 `docs/technical-design.md` 第 19 节。

**先扩展已有机制。** 新功能优先落在既有的分层和模式上（Action → Tool/Command 适配、Port 加组合根注入），不为单次需求另起一套平行的机制。

**行为变了就补测试。** 已有公开接缝的地方，为改动补上有针对性的测试。

**文档跟着代码走。** 改动影响了 `docs/technical-design.md` 或 `docs/tech-max/` 某一卷描述的事实，在同一个 PR 里更新，过时的句子直接改写，不要只在末尾追加。

## 提交信息

格式是 `类型: 简短的中文描述`，类型用 `feat`、`fix`、`refactor`、`test`、`docs`、`chore`。

标题只说做了什么，一行能读完（建议不超过 50 个字）。原因、取舍、影响面和测试情况写在正文里，标题和正文之间空一行：

```text
fix: 判定器超时由 15 秒放宽到 60 秒

线上 102 次调用中位数 8.1 秒，其中 9 次撞上 15 秒被掐掉、回退基线。
只改一个常量与一句注释，不加配置键。
```

## 不要提交的东西

- 密钥、令牌、`build/.config.json`、数据库、日志、生成的图片和语音。
- 真实的 QQ 号、群号、群名、他人的昵称，以及真实的聊天内容。测试和文档里需要例子时用虚构的。
- 只在你机器上成立的绝对路径，以及 `CMakeUserPresets.json`。

发现安全问题时，请不要在公开的 issue 里贴出细节，先私下联系维护者。

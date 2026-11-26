## 做了什么

<!-- 一两句话。一个 PR 只做一件事。 -->

## 为什么

<!-- 起因、现象或数据；有取舍的话写明选了什么、放弃了什么。 -->

## 设计自查

评审按 `docs/design-philosophy.md` 进行，先自己过一遍。

- [ ] 删除测试：把这次加的功能整个删掉，核心（`Message`、`ChatService`、`Application/ToolLoop`、`UserSessionService`、`Port/` 等）不需要跟着改
- [ ] 复用了现有的机制 <!-- 写出是哪个；没有复用的话，说明现有机制为什么装不下 -->
- [ ] 如果改了核心：加的是与这个功能无关的通用能力 <!-- 说明谁还会用到 -->

## 怎么验证的

- [ ] `ctest --preset linux-debug` 全部通过
- [ ] `git diff --check` 没有输出
- [ ] 行为有变化的地方补了测试（没补的话说明原因）

<!-- 需要真实 QQ / 模型联调才能确认的部分，写明有没有手动试过。 -->

## 关联点自查

只保留这次改动涉及的行，其余删掉。各类的完整清单见 `AGENTS.md` 的“关联点必须一次改全”。

- [ ] **配置键**：`SchemaConfig.h`、`ConfigLoader.cpp`、`ConfigTemplate.cpp`、`config.example.json`、`RuntimeSettings`、规则表 `ConfigRules.cpp`、`panel.html`、相关测试、卷 04 与 README；示例配置的自检没有未知字段告警
- [ ] **入站消息字段**：`Port/InboundMessage.h`、`OneBotEventDecoder`、卷 05 与卷 06
- [ ] **SQLite 表**：对应 Store 的建表语句、卷 13、集成测试
- [ ] **线程或调度点**：`main.cpp` 的生命周期与退出链、卷 22、`technical-design.md` §4.2
- [ ] **新增或删除模块、源文件**：卷 01 的目录地图、`architecture_dependency_test.py`、卷 21 的测试清单
- [ ] **计数或枚举变了**：全仓搜过这个数字或枚举名
- [ ] **文档**：`docs/technical-design.md` 与相关的 tech-max 卷已同步

## 提交前确认

- [ ] 没有密钥、真实的 QQ 号或群号、真实的聊天内容、只在本机成立的路径

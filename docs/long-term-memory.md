# KleinBot 长期记忆

## 目标

长期记忆用于保存当前上下文窗口之外、未来对话仍可能有价值的信息。原始对话仍然是事实来源，长期记忆只是从原始对话提取出的可检索摘要，不替代 `conversations` 表。

当前版本不使用 Embedding。语义能力来自模型生成的规范事实、同义表达和关键词，底层使用 SQLite `LIKE` 检索较小规模的长期记忆表。

## 数据流

```text
用户消息 + 助手回复
        ↓
写入 conversations
        ↓
提交到 MemoryService
        ↓
累计 MEMORY_BATCH_TURNS 轮，或空闲 MEMORY_IDLE_SECONDS 秒
        ↓
MemoryExtractor 调用 MEMORY_MODEL
        ↓
生成结构化记忆 mutation
        ↓
MemoryStore UPSERT / deactivate
```

记忆提取在独立后台线程执行，不阻塞当前聊天回复。关闭程序时不会继续启动新的提取任务；尚未达到触发条件的内存批次会丢弃，但原始对话仍保存在 SQLite 中。

## 记忆结构

`memories` 表包含以下主要字段：

| 字段 | 说明 |
| --- | --- |
| `user_id` | 用户隔离键 |
| `memory_key` | 稳定事实键，同一事实变化时复用 |
| `memory_type` | profile、preference、relationship、event、state、decision、task、technical |
| `canonical_text` | 规范化后的事实描述 |
| `search_text` | 规范事实、同义表达和关键词 |
| `importance` | 未来对话的重要程度，0 到 1 |
| `confidence` | 从原始对话确认该事实的可信度，0 到 1 |
| `source_start_id` | 来源原始消息起始 ID |
| `source_end_id` | 来源原始消息结束 ID |
| `active` | 当前是否有效 |

数据库使用 `(user_id, memory_key)` 唯一约束。稳定事实发生变化时执行 UPSERT，例如：

```text
preference.favorite_game = 原神
                    ↓
preference.favorite_game = 塞尔达传说
```

事件类型应使用包含日期或唯一标识的 key，避免不同事件互相覆盖。

## 召回流程

`recall_conversation` 工具要求模型生成 1 到 5 个同义检索短语：

```json
{
  "queries": [
    "失眠问题",
    "睡不着",
    "睡眠不好"
  ],
  "limit": 8
}
```

查询流程：

```text
搜索 memories.canonical_text / memories.search_text
        ↓
按 importance 和 updated_at 排序
        ↓
根据 source_start_id / source_end_id 读取原始对话证据
        ↓
没有长期记忆命中时回退 conversations LIKE 查询
```

模型返回的 `%`、`_` 和反斜杠会被转义，不会被当作 LIKE 通配符。

## 配置

可在 `config.json` 任意一级配置对象中加入：

```json
{
  "长期记忆": {
    "MEMORY_ENABLED": true,
    "MEMORY_MODEL": "glm-5.2",
    "MEMORY_BATCH_TURNS": 3,
    "MEMORY_IDLE_SECONDS": 20,
    "MEMORY_RECALL_LIMIT": 8
  }
}
```

- `MEMORY_ENABLED`：默认 `true`。
- `MEMORY_MODEL`：必须是 `ModelsName.json` 已注册的模型；未配置时使用 `DEFAULT_MODEL`。
- `MEMORY_BATCH_TURNS`：批次越小，记忆更新越快，但模型调用次数越多。
- `MEMORY_IDLE_SECONDS`：用户停止连续对话后，未满批次仍会触发提取。
- `MEMORY_RECALL_LIMIT`：限制返回给聊天模型的长期记忆数量。

## 删除一致性

- `#重置对话`：删除该用户全部原始对话和长期记忆，同时取消等待提取的批次。
- 删除最近上下文：失效来源消息位于删除区间内的长期记忆。
- 提取进行期间发生删除：通过用户 epoch 丢弃已经过期的提取结果，避免旧事实重新写回。

当前使用 UPSERT 保存事实的最新版本，没有保存同一 `memory_key` 的历史版本。因此删除最新来源后，对应记忆会失效，但不会自动恢复到更早的旧值。

## 手工验证建议

1. 将 `MEMORY_BATCH_TURNS` 设置为 `1`，便于快速观察。
2. 和机器人说明一个稳定偏好，例如“我最喜欢的游戏是塞尔达传说”。
3. 等待超过 `MEMORY_IDLE_SECONDS`，或继续完成一个批次。
4. 检查 SQLite `memories` 表是否生成对应记录。
5. 清除当前上下文后，询问“我以前说过最喜欢什么游戏”。
6. 确认工具返回规范记忆和原始对话证据。
7. 使用 `#重置对话`，确认该用户的长期记忆被清除。

## 后续升级

当长期记忆数量增长后，可以保持 `MemoryService` 接口不变，将 `MemoryStore::search` 从 `LIKE` 替换为 FTS5。未来若增加 Embedding，也建议与结构化字段和全文检索并行，而不是替代原始对话和长期记忆模型。

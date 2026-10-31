#ifndef TOOL_NAMES_H
#define TOOL_NAMES_H

/*
 * 模型可调用工具的名字（唯一定义处，ADR-022）。
 * 工具/Action 的 name()、组合根的开放名单、工具说明组装器的排序表都引用这里，
 * 任何模块都不必为了一个工具名去依赖实现该工具的模块（例如为 klein_web_fetch
 * 去包含 WebFetch/）。Port 层只放常量，不依赖任何项目内模块。
 */

// 图片三工具
inline constexpr const char *KleinInspectImageToolName = "inspect_image";
inline constexpr const char *KleinSendImageToolName = "send_image";
inline constexpr const char *KleinGenerateImageToolName = "generate_image";

// 记忆召回
inline constexpr const char *KleinRecallConversationToolName = "recall_conversation";

// 会话 / 管理
inline constexpr const char *KleinGetCurrentModelToolName = "get_current_model";
inline constexpr const char *KleinSetVoiceModeToolName = "set_voice_mode";
inline constexpr const char *KleinAdminControlToolName = "admin_control";

// 联网（klein_ 前缀：避免第三方网关抢占保留名 web_search / web_fetch）
inline constexpr const char *KleinWebSearchToolName = "klein_web_search";
inline constexpr const char *KleinWebFetchToolName = "klein_web_fetch";

// 提醒
inline constexpr const char *KleinSetReminderToolName = "set_reminder";
inline constexpr const char *KleinListRemindersToolName = "list_reminders";
inline constexpr const char *KleinCancelReminderToolName = "cancel_reminder";

#endif // TOOL_NAMES_H

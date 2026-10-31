#ifndef TOOL_GUIDANCE_H
#define TOOL_GUIDANCE_H

/*
 * 工具使用规则的唯一组装点（ADR-022 机制复用）：规则文本由各工具自带
 * （Tool::usageGuidance / Action::usageGuidance），这里只负责"按什么顺序、什么框架"拼。
 * ChatService（Conversation）与 EngagementService（GroupTurn）共用，调用方不再按工具名 if。
 *
 * 固定顺序（改动即改变 system prompt 前缀，作废全部前缀缓存，必须同步金样测试）：
 *   Conversation：
 *     1. 固定前导 "\n\n"
 *     2. inspect_image / send_image / generate_image（三者共用一段，去重后出现一次）
 *     3. recall_conversation
 *     4. get_current_model
 *     5. set_voice_mode
 *     6. admin_control
 *     7. 固定非工具句："不要把重置对话、重置上下文、设置人格或还原人格转换为工具调用。"
 *     8. klein_web_search（含运行时本地日期锚点）
 *     9. klein_web_fetch
 *    10. set_reminder / list_reminders / cancel_reminder
 *   GroupTurn：
 *     1. inspect_image / generate_image / send_image（共用一段，去重）
 *     2. 联网分组：klein_web_search、klein_web_fetch 的引导句，任一开放时整体包成
 *        "\n\n[系统注] " + 引导句… + kWebToolsGroupTurnNote
 *   两个场景之后：布局里没有的其它工具，按 offeredNames 顺序接在最后（新工具无需改这里）。
 *
 * 只有既在 offeredNames 里、又在注册表里找得到的工具才参与；规则为空的工具不占位；
 * 完全相同的规则文本只输出一次。
 */
#include "../Port/UsageGuidance.h"

#include <string>
#include <vector>

class ToolRegistry;

std::string composeToolGuidance(const ToolRegistry &tools, const std::vector<std::string> &offeredNames,
                                GuidanceScene scene, const GuidanceContext &context);

#endif // TOOL_GUIDANCE_H

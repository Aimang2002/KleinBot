#include "ToolGuidance.h"
#include "ReminderToolNames.h"
#include "WebSearchRouting.h"
#include "../Action/WebToolGuidance.h"
#include "../Tool/ToolRegistry.h"
#include "../WebFetch/WebFetchOptions.h"

#include <algorithm>

namespace
{
// 布局的一段：tools 为空时是固定文本段（header 即文本，总是输出）；否则段内任一工具
// 给出非空规则时输出 header + 各规则 + footer
struct GuidanceSection
{
    std::vector<std::string> tools;
    std::string header;
    std::string footer;
};

// 顺序说明见 ToolGuidance.h；改动必须同步 tests/ToolGuidanceTests.cpp 的金样
const std::vector<GuidanceSection> &layoutFor(GuidanceScene scene)
{
    static const std::vector<GuidanceSection> conversation = {
        {{}, "\n\n", ""},
        {{"inspect_image", "send_image", "generate_image"}, "", ""},
        {{"recall_conversation"}, "", ""},
        {{"get_current_model"}, "", ""},
        {{"set_voice_mode"}, "", ""},
        {{"admin_control"}, "", ""},
        {{}, "不要把重置对话、重置上下文、设置人格或还原人格转换为工具调用。", ""},
        {{KleinWebSearchToolName}, "", ""},
        {{KleinWebFetchToolName}, "", ""},
        {{KleinSetReminderToolName, KleinListRemindersToolName, KleinCancelReminderToolName}, "", ""},
    };
    static const std::vector<GuidanceSection> groupTurn = {
        {{"inspect_image", "generate_image", "send_image"}, "", ""},
        {{KleinWebSearchToolName, KleinWebFetchToolName}, "\n\n[系统注] ", kWebToolsGroupTurnNote},
    };
    return scene == GuidanceScene::Conversation ? conversation : groupTurn;
}

bool contains(const std::vector<std::string> &values, const std::string &value)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}
} // namespace

std::string composeToolGuidance(const ToolRegistry &tools, const std::vector<std::string> &offeredNames,
                                GuidanceScene scene, const GuidanceContext &context)
{
    std::vector<std::string> emitted; // 已输出的规则文本（共用规则只出一次）
    std::vector<std::string> placed;  // 已处理过的工具名
    const auto guidanceOf = [&](const std::string &name) -> std::string
    {
        placed.push_back(name);
        if (!contains(offeredNames, name))
            return {};
        const Tool *tool = tools.find(name);
        if (tool == nullptr)
            return {};
        std::string text = tool->usageGuidance(scene, context);
        if (text.empty() || contains(emitted, text))
            return {};
        emitted.push_back(text);
        return text;
    };

    std::string result;
    for (const GuidanceSection &section : layoutFor(scene))
    {
        if (section.tools.empty())
        {
            result += section.header;
            continue;
        }
        std::string body;
        for (const std::string &name : section.tools)
            body += guidanceOf(name);
        if (!body.empty())
            result += section.header + body + section.footer;
    }
    // 布局之外的工具：按开放顺序接在最后
    for (const std::string &name : offeredNames)
    {
        if (!contains(placed, name))
            result += guidanceOf(name);
    }
    return result;
}

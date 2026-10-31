#ifndef IMAGE_TOOL_GUIDANCE_H
#define IMAGE_TOOL_GUIDANCE_H

/*
 * 图片三工具（inspect_image / send_image / generate_image）共用的使用规则（ADR-022）。
 * 规则讲的是三者如何配合，因此三个工具返回同一段文本；composeToolGuidance 对相同文本
 * 去重，任一图片工具开放即出现一次。
 */
#include "../Port/UsageGuidance.h"

#include <string>

inline std::string imageToolsUsageGuidance(GuidanceScene scene)
{
    if (scene == GuidanceScene::Conversation)
    {
        return "图片上下文规则：历史消息中的 [image asset_id=...] 只是资源引用，你当前并未直接看到图片。"
               "当用户询问历史图片的内容、位置、文字、颜色或细节时，必须调用 inspect_image；"
               "当用户要求重新发送历史图片时调用 send_image；当用户要求生成图片时调用 generate_image。"
               "generate_image 和 send_image 会直接发送图片，调用成功后不要再次调用任何图片工具，也不要把 asset_id 占位符展示给用户。"
               "历史中的图片占位符只代表过去已经发送的图片，不能当作本轮新生成结果。"
               "用户表示不满意、要求修改、换一版或重新生成时，必须调用 generate_image 生成新图片，不能只引用或重发旧图片。"
               "不得在未调用 inspect_image 时猜测图片细节。"
               "inspect_image 返回的描述无法识别具体人物或内容时，如实转述看到的特征即可，"
               "不得把工具回答的不确定性说成技术故障或系统错误。";
    }
    // 群会话轮：材料里的图片占位符只是瞥视印象（R1.1 GlanceService 写回），需要细节时去看图本身；
    // 生图/重发图的结果当场发到群里（2026-10-30 真机：点名让她画图，她只能嘴上说）
    return "\n\n[系统注] 材料里 [image asset_id=… source=inbound | …] 占位符中 | 之后那句话是你当时瞥了一眼的印象，不是图片本身。"
           "需要看清细节（文字、人物、局部、第二张/那张图）时调用 inspect_image 并带上 asset_id；"
           "不确定是哪张就省略 asset_id，系统会取最近一张。"
           "generate_image 会直接把图发到群里，send_image 可以重发群里 24 小时内的图，调用成功后不必再描述那张图。";
}

#endif // IMAGE_TOOL_GUIDANCE_H

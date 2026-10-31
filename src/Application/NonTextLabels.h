#ifndef NON_TEXT_LABELS_H
#define NON_TEXT_LABELS_H

/*
 * 入站消息里非文字段的文字描述（ADR-022 机制复用）：群缓冲（GroupContextService）与
 * @ 聊天路径（Message）共用同一份渲染，新增段类型只改这里。
 *   - 表情：mface 的 summary（常自带方括号 "[狗头]"，去掉外层）与 face 的 "face:<id>"
 *     统一包成 "[表情 …]"；只有群缓冲需要（私聊/@ 路径历来不读表情摘要）
 *   - 附件：解码器给出的 attachment_labels（"[视频]"、"[文件 名]"、"[卡片 …]" …）原样
 * 图片不在这里：图片要入库拿 asset_id，占位符见 Asset/ImagePlaceholder.h。
 */
#include "../Port/InboundMessage.h"

#include <string>
#include <vector>

inline std::vector<std::string> nonTextLabels(const InboundMessage &message, bool includeFaces)
{
    std::vector<std::string> labels;
    if (includeFaces)
    {
        for (const std::string &face : message.face_summaries)
        {
            std::string label = face;
            if (label.size() >= 2 && label.front() == '[' && label.back() == ']')
                label = label.substr(1, label.size() - 2);
            if (!label.empty())
                labels.push_back("[表情 " + label + "]");
        }
    }
    for (const std::string &label : message.attachment_labels)
    {
        if (!label.empty())
            labels.push_back(label);
    }
    return labels;
}

// 空格连接
inline std::string joinLabels(const std::vector<std::string> &labels)
{
    std::string joined;
    for (const std::string &label : labels)
        joined += (joined.empty() ? "" : " ") + label;
    return joined;
}

// 逐个追加到文本后（文本为空时不留前导空格）
inline void appendLabels(std::string &text, const std::vector<std::string> &labels)
{
    for (const std::string &label : labels)
        text = text.empty() ? label : (text + " " + label);
}

#endif // NON_TEXT_LABELS_H

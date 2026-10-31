#ifndef IMAGE_PLACEHOLDER_H
#define IMAGE_PLACEHOLDER_H

/*
 * 图片占位符的唯一格式化/解析点（ADR-013 资源占位符、ADR-020 瞥视描述）。
 * 会话历史、群缓冲、工具上下文注记里的图片都写成同一种文本：
 *     [image asset_id=X source=inbound]
 *     [image asset_id=X source=inbound sticker]       群里以表情包形式发送
 *     [image asset_id=X source=generated]
 *     [image asset_id=X source=inbound | 一行描述]     瞥视层写回的派生描述
 * 任何地方都不得再手拼这段字符串：格式一变，模型规则文本、瞥视写回与工具解析
 * 必须同时变，只有集中在这里才改得全。
 */
#include <string>

// source 取资产来源（"inbound"/"generated"）；sticker=true 追加 " sticker" 标记
inline std::string formatImagePlaceholder(const std::string &assetId, const std::string &source,
                                          bool sticker = false)
{
    return "[image asset_id=" + assetId + " source=" + source + (sticker ? " sticker" : "") + "]";
}

// 把 " | <caption>" 插进 text 里该资产占位符的 ] 之前。
// 只认完整 id（gimg_1 不会命中 gimg_12）；占位符已带描述时不重复追加。
// 无事可做（找不到占位符 / 已有描述 / 参数为空）返回空串
inline std::string attachImageCaption(const std::string &text, const std::string &assetId,
                                      const std::string &caption)
{
    if (assetId.empty() || caption.empty())
        return {};
    const std::string marker = "[image asset_id=" + assetId;
    const std::size_t begin = text.find(marker);
    if (begin == std::string::npos)
        return {};
    const std::size_t after = begin + marker.size();
    // 必须是完整 id（防止 gimg_1 命中 gimg_12）
    if (after >= text.size() || (text[after] != ' ' && text[after] != ']'))
        return {};
    const std::size_t close = text.find(']', after);
    if (close == std::string::npos)
        return {};
    // 已有描述（重复写回）不再追加
    const std::size_t existing = text.find(" | ", after);
    if (existing != std::string::npos && existing < close)
        return {};
    return text.substr(0, close) + " | " + caption + text.substr(close);
}

#endif // IMAGE_PLACEHOLDER_H

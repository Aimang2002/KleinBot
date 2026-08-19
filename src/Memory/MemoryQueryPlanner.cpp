#include "MemoryQueryPlanner.h"
#include <array>

namespace
{
template <std::size_t Size>
bool containsAny(const std::string &text, const std::array<const char *, Size> &terms)
{
    for (const char *term : terms)
    {
        if (text.find(term) != std::string::npos)
            return true;
    }
    return false;
}
}

bool hasExplicitRecallIntent(const std::string &text)
{
    static constexpr std::array recallTerms = {
        "以前", "之前", "曾经", "过去", "上次", "原来", "最早", "一开始",
        "还记得", "记不记得", "说过", "提过", "告诉过", "聊过", "历史",
        "当时", "后来", "改之前", "上一版", "上一个"};
    return containsAny(text, recallTerms);
}

bool looksLikeFactQuestion(const std::string &text)
{
    static constexpr std::array questionTerms = {
        "是什么", "叫什么", "是谁", "哪一个", "哪个", "哪里", "在哪", "多少",
        "多大", "几岁", "什么时候", "哪年", "住哪", "用什么", "用的是",
        "喜欢什么", "最喜欢", "现在是", "目前是", "配置是什么", "什么配置",
        "版本是多少", "什么版本", "有没有"};
    return containsAny(text, questionTerms);
}

std::string detectFactTemporal(const std::string &text)
{
    static constexpr std::array timelineTerms = {
        "时间线", "变化过程", "历史记录", "都用过", "全部版本", "怎么变", "哪些版本"};
    if (containsAny(text, timelineTerms))
        return "timeline";

    static constexpr std::array previousTerms = {
        "上一个", "上一版", "改之前", "更新之前", "之前那个", "前一个"};
    if (containsAny(text, previousTerms))
        return "previous";

    static constexpr std::array earliestTerms = {
        "最早", "一开始", "最开始", "起初", "原先", "最初"};
    if (containsAny(text, earliestTerms))
        return "earliest";
    return "current";
}

#ifndef SPOKEN_FORM_H
#define SPOKEN_FORM_H

/*
 * 她要说的一句话以什么形式送达（ADR-024：内容与送达分离）。Message 的输出决策（文本/语音）
 * 唯一判定点：内容只有一份，形式可以退——想用语音但合成失败，就把原话用文字发出去，
 * 失败永远不顶替内容。
 * 2026-11-16 真机：语音合成服务没开，她回答 @ 的“好，我退开了”被换成
 * “系统提示：语音模块异常。”发进群里，原话丢了。
 *
 * 纯函数：合成经调用方传入的 seam，不记日志（退回文字时由调用方记一条告警）。
 */
#include <functional>
#include <string>
#include <utility>

struct SpokenForm
{
    bool voice = false;       // true：发语音（audioPath）；false：发原文
    std::string audioPath;
    bool voiceFailed = false; // 想用语音但合成失败，已退回文字
};

// 返回合成好的音频文件路径，失败返回空串
using SpeechSynthesizer = std::function<std::string(const std::string &text)>;

inline SpokenForm chooseSpokenForm(const std::string &text, bool wantVoice,
                                   const SpeechSynthesizer &synthesize)
{
    SpokenForm form;
    if (!wantVoice)
        return form;
    std::string audioPath = synthesize ? synthesize(text) : std::string();
    if (audioPath.empty())
    {
        form.voiceFailed = true;
        return form;
    }
    form.voice = true;
    form.audioPath = std::move(audioPath);
    return form;
}

#endif // SPOKEN_FORM_H

#ifndef PERSONA_REPLIER_H
#define PERSONA_REPLIER_H

#include <cstdint>
#include <functional>
#include <string>

// 人格化单轮回应 seam（T6）：事件回应 handler 只依赖这个函数类型，
// 组合根绑 ChatService::replyInCharacter，测试注入 fake（同 CapabilityBroker::VersionProbe 惯例）。
// 失败/模型未配置返回空串，由调用方决定降级行为（不发或兜底文案）
using PersonaReplier = std::function<std::string(std::uint64_t user_id, const std::string &prompt)>;

#endif // PERSONA_REPLIER_H

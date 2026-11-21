#ifndef REFLECT_ACTION_H
#define REFLECT_ACTION_H

#include "Action.h"
#include "../Affect/AffectOptions.h"
#include "../utils/Live.h"

#include <cstdint>

namespace affect
{
class ReflectionService;
}

// 手动回想（经历层 E3，ADR-030；设计 §6.2 “部署者命令手动触发”）：reflect_now。
// 部署者在管理员私聊里说“你回想一下最近在群里的事”，她觉得是在叫她回想就调它；工具只是把那个群
//（或所有有待回想经历的群）排进回想服务的队列，回想在后台线程上跑，结果之后用 review_guidance 翻看。
// 与账本工具同一开放规则：只在管理员私聊里出现（managerPrivateOnly + requires_admin），affect.enabled
// 当前为假时不可用。描述只写机制。回复是给模型看的短句，由她自己用自己的话说
class ReflectAction : public Action
{
public:
    explicit ReflectAction(const Live<AffectOptions> &options);

    // 回想服务由组合根在接线后 bind（它构造得比工具表晚）；未 bind 时工具不可用
    void bind(affect::ReflectionService *service) { this->service = service; }

    const ActionDescriptor &descriptor() const override;
    bool available() const override;
    bool managerPrivateOnly() const override { return true; }
    ActionResult execute(const nlohmann::json &arguments, const ActionContext &context) override;

private:
    const Live<AffectOptions> &options;
    affect::ReflectionService *service = nullptr;
};

#endif // REFLECT_ACTION_H

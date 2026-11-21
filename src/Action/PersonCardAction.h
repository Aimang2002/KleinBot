#ifndef PERSON_CARD_ACTION_H
#define PERSON_CARD_ACTION_H

#include "Action.h"

class PersonCards;

// 写人物卡（ADR-032）：update_person_card。对方说“以后叫我小白”，她觉得该记就调它；当场写库、
// 缓存失效，下一次请求里的卡片就是新的。私聊（任何人）与群聊都开放，不要求管理员：卡片是她自己的
// 记事本，记不记、怎么叫由她定。可写的只有四类槽位；QQ 号、昵称、是不是部署者、初次记下的日期由程序
// 给出，不在可写范围里——没有人能靠说一句话变成部署者。
// user 不填时写的是正在跟她说话的人（私聊对象 / @ 她的人）；会话轮里没有“正在跟她说话的人”，必须填。
// 长期记忆不可用或 memory.enabled 为假时工具不出现。描述只讲机制；回复是给模型看的短句
class UpdatePersonCardAction : public Action
{
public:
    explicit UpdatePersonCardAction(PersonCards &cards);

    const ActionDescriptor &descriptor() const override;
    bool available() const override;
    ActionResult execute(const nlohmann::json &arguments, const ActionContext &context) override;

private:
    PersonCards &cards;
};

#endif // PERSON_CARD_ACTION_H

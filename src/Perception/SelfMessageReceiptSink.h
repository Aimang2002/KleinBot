#ifndef SELF_MESSAGE_RECEIPT_SINK_H
#define SELF_MESSAGE_RECEIPT_SINK_H

#include "GroupContextStore.h"
#include "../Port/DeliveryReceipt.h"

// 群自消息 message_id 回填（R0.2 步骤 7）：Klein 自己的群发言在入库时没有
// message_id（协议端还没答复），回执 sent 到达后按 correlation_id 补上——
// EngagementService::replyToSelf（"引用回复她"触发）由此第一次真正可用。
// store 可空（观察通道关闭）时 no-op
class SelfMessageReceiptSink final : public DeliveryReceiptSink
{
public:
    explicit SelfMessageReceiptSink(GroupContextStore *store) : store(store) {}

    void onReceipt(DeliveryReceipt receipt) override
    {
        if (store == nullptr || receipt.status != "sent" || receipt.message_id_raw.empty())
            return;
        store->backfillMessageId(receipt.correlation_id, receipt.message_id_raw);
    }

private:
    GroupContextStore *const store;
};

#endif // SELF_MESSAGE_RECEIPT_SINK_H

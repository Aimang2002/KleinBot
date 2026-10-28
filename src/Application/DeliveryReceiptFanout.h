#ifndef DELIVERY_RECEIPT_FANOUT_H
#define DELIVERY_RECEIPT_FANOUT_H

#include "../Port/DeliveryReceipt.h"

#include <vector>

// 回执扇出：一次投递回执广播给多个消费者（affect 执行记录、群自消息回填…）。
// 组合根在线程启动前 addSink 完毕，之后只读；消费者各自保证线程安全
class DeliveryReceiptFanout final : public DeliveryReceiptSink
{
public:
    void addSink(DeliveryReceiptSink *sink)
    {
        if (sink != nullptr)
            sinks.push_back(sink);
    }

    void onReceipt(DeliveryReceipt receipt) override
    {
        for (DeliveryReceiptSink *sink : sinks)
            sink->onReceipt(receipt);
    }

private:
    std::vector<DeliveryReceiptSink *> sinks;
};

#endif // DELIVERY_RECEIPT_FANOUT_H

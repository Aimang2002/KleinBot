#ifndef DELIVERY_RECEIPT_H
#define DELIVERY_RECEIPT_H

#include <cstdint>
#include <string>

// 出站回执（R0.2 / B2）：协议端对一次业务投递的真实答复。
// 只有 OutboundDelivery.correlation_id 非空的投递才会产生回执；
// 空 correlation_id 的投递保持 fire-and-forget，传输层行为逐字不变。
//
// status 三值语义（拿不到回执一律 unknown，绝不伪造 sent）：
//   sent    协议端明确 status=ok / retcode=0，message_id_raw 为协议端返回的消息 ID（可空）
//   failed  协议端明确拒绝（status=failed / 非 2xx / retcode≠0 / 写出失败）
//   unknown 超时、断连、通道异常——是否送达无法判断
struct DeliveryReceipt
{
    std::string correlation_id;
    std::string status;
    std::string message_id_raw; // 协议端消息 ID 原样（数字或字符串形态）
    std::int64_t retcode = 0;
    std::string detail;         // 人读说明（日志用），不进业务判断
};

// 回执消费端口：由组合根装配（affect 执行记录回填、群自消息 message_id 回填…）；
// 传输线程调用，实现必须线程安全且不得阻塞（不要在里面做模型调用或长 IO）
class DeliveryReceiptSink
{
public:
    virtual ~DeliveryReceiptSink() = default;

    virtual void onReceipt(DeliveryReceipt receipt) = 0;
};

#endif // DELIVERY_RECEIPT_H

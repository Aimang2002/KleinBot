#ifndef LIVE_H
#define LIVE_H

/*
 * 活值：一份可整体替换的不可变配置快照（ADR-023，配置实时生效）。
 *
 * - 互斥量只保护一个 shared_ptr<const T>，写法与 ConfigSnapshotStore::current() 一致；
 *   锁只在取、换指针的瞬间持有，不与任何业务锁嵌套。
 * - get() 返回的快照拿到手就不会变：替换发生时，正在进行的操作继续用自己手里的
 *   那一份，用完自动释放；新值从下一次 get() 开始可见。set 之前多次 get() 拿到的
 *   是同一个指针，使用方可以比较快照指针判断配置有没有换过。
 * - 使用方只持 const Live<T> &：只能 get()，调不了 set()（非 const 成员）。
 *   set() 只由持有者（组合根）在配置重载成功后调用。
 * - 不提供 ->、解引用和隐式转换：必须先显式取一份快照，一次操作开头取一次、
 *   之后都用这一份，避免同一次操作里前后读到两个版本。
 * - 不可拷贝，不带回调。需要按配置维护派生状态的模块，在操作开头比较快照指针，
 *   变了就在自己的锁内重建。
 */
#include <memory>
#include <mutex>
#include <utility>

template <typename T>
class Live
{
public:
    using Snapshot = std::shared_ptr<const T>;

    explicit Live(T initial) : current(std::make_shared<T>(std::move(initial))) {}

    Live(const Live &) = delete;
    Live &operator=(const Live &) = delete;

    // 取当前快照：一次操作开头取一次，之后都用这一份
    Snapshot get() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return current;
    }

    // 整份替换：新快照在锁外构造，锁内只交换指针；旧快照在锁外释放
    // （仍被别处持有时由最后一个持有者释放）
    void set(T next)
    {
        Snapshot replacement = std::make_shared<T>(std::move(next));
        {
            std::lock_guard<std::mutex> lock(mutex);
            current.swap(replacement);
        }
    }

private:
    mutable std::mutex mutex;
    Snapshot current;
};

#endif // LIVE_H

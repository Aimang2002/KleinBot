#include <gtest/gtest.h>

#include "utils/Live.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace
{
// 接口形态由编译器保证（ADR-023）：使用方持 const Live<T> &，只能显式取快照
template <typename L, typename = void>
struct HasArrow : std::false_type
{
};
template <typename L>
struct HasArrow<L, std::void_t<decltype(std::declval<const L &>().operator->())>>
    : std::true_type
{
};

template <typename L, typename = void>
struct HasDereference : std::false_type
{
};
template <typename L>
struct HasDereference<L, std::void_t<decltype(*std::declval<const L &>())>> : std::true_type
{
};

template <typename L, typename = void>
struct SettableThroughConstReference : std::false_type
{
};
template <typename L>
struct SettableThroughConstReference<
    L, std::void_t<decltype(std::declval<const L &>().set(std::declval<int>()))>>
    : std::true_type
{
};

static_assert(!std::is_copy_constructible_v<Live<int>>, "活值不可拷贝");
static_assert(!std::is_copy_assignable_v<Live<int>>, "活值不可拷贝赋值");
static_assert(!std::is_convertible_v<int, Live<int>>, "构造函数是 explicit");
static_assert(!std::is_convertible_v<const Live<int> &, int>, "没有到值的隐式转换");
static_assert(!std::is_convertible_v<const Live<int> &, std::shared_ptr<const int>>,
              "没有到快照的隐式转换");
static_assert(!HasArrow<Live<int>>::value, "不提供 ->");
static_assert(!HasDereference<Live<int>>::value, "不提供解引用");
static_assert(!SettableThroughConstReference<Live<int>>::value, "const 引用调不了 set");
static_assert(std::is_same_v<decltype(std::declval<const Live<int> &>().get()),
                             std::shared_ptr<const int>>,
              "get 返回不可变快照");

// 并发用例的载荷：三个字段由同一个 value 推出，读到的快照只要有一处对不上，
// 就说明读到了没写完的对象
struct Stamp
{
    std::uint64_t value = 0;
    std::uint64_t inverted = ~std::uint64_t{0};
    std::string text = "0";
};

Stamp stampOf(std::uint64_t value)
{
    return Stamp{value, ~value, std::to_string(value)};
}

bool consistent(const Stamp &stamp)
{
    return stamp.inverted == ~stamp.value && stamp.text == std::to_string(stamp.value);
}
}

TEST(LiveTest, InitialValueIsReadable)
{
    const Live<std::string> live(std::string("初值"));
    const Live<std::string>::Snapshot snapshot = live.get();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(*snapshot, "初值");
}

TEST(LiveTest, GetReturnsTheSameSnapshotUntilSet)
{
    Live<int> live(7);
    const Live<int>::Snapshot first = live.get();
    const Live<int>::Snapshot second = live.get();
    EXPECT_EQ(first, second) << "没有替换时取到的是同一份快照，使用方可以比较指针判断是否换过";

    live.set(7);
    EXPECT_NE(live.get(), first) << "set 总是换一份新快照，即使值相同";
}

TEST(LiveTest, SetPublishesNewSnapshotAndKeepsOldSnapshotIntact)
{
    Live<std::vector<int>> live(std::vector<int>{1, 2, 3});
    const Live<std::vector<int>>::Snapshot before = live.get();

    live.set(std::vector<int>{4, 5});

    const Live<std::vector<int>>::Snapshot after = live.get();
    ASSERT_NE(after, nullptr);
    EXPECT_EQ(*after, (std::vector<int>{4, 5}));
    EXPECT_NE(before, after);
    // 替换之前取到的那份内容不变：正在进行的操作继续用旧快照
    EXPECT_EQ(*before, (std::vector<int>{1, 2, 3}));
}

TEST(LiveTest, ConcurrentReadersAlwaysSeeACompleteWrite)
{
    Live<Stamp> live(stampOf(0));
    std::atomic<bool> writing{true};
    std::atomic<std::uint64_t> reads{0};
    std::atomic<std::uint64_t> broken{0};

    std::vector<std::thread> readers;
    for (int index = 0; index < 4; ++index)
    {
        readers.emplace_back([&]() {
            do
            {
                const Live<Stamp>::Snapshot snapshot = live.get();
                if (snapshot == nullptr || !consistent(*snapshot))
                    broken.fetch_add(1);
                reads.fetch_add(1);
            } while (writing.load());
        });
    }

    constexpr std::uint64_t kWritesPerWriter = 20000;
    std::vector<std::thread> writers;
    for (std::uint64_t writer = 0; writer < 2; ++writer)
    {
        writers.emplace_back([&live, writer]() {
            for (std::uint64_t index = 1; index <= kWritesPerWriter; ++index)
                live.set(stampOf(index * 2 + writer));
        });
    }
    for (std::thread &thread : writers)
        thread.join();
    writing.store(false);
    for (std::thread &thread : readers)
        thread.join();

    EXPECT_EQ(broken.load(), 0U);
    EXPECT_GT(reads.load(), 0U);
    const Live<Stamp>::Snapshot last = live.get();
    ASSERT_NE(last, nullptr);
    EXPECT_TRUE(consistent(*last));
    // 最后一次写入来自两个写线程之一的最后一笔
    EXPECT_TRUE(last->value == kWritesPerWriter * 2 || last->value == kWritesPerWriter * 2 + 1)
        << last->value;
}

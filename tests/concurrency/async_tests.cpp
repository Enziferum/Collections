#include <gtest/gtest.h>
#include <collections/concurrency/future.hpp>
#include <collections/concurrency/thread_pool.hpp>

using namespace std::chrono_literals;

namespace {
    class ConcurrencyTest: public ::testing::Test {
    public:
        void TearDown() override {
            pool.waitIdle();
            pool.join();
        }
    protected:
        concurrency::thread_pool pool{4};
    };
}


TEST_F(ConcurrencyTest, test_async_subscribe) {
    auto testId = std::this_thread::get_id();

    auto f = concurrency::async(pool, []() -> int {
        std::this_thread::sleep_for(1s);
        return 41;
    }).subscribe(pool, [](concurrency::Result<int>&& result) {
        int value = result.ValueOrThrow() + 1;
        EXPECT_EQ(value, 42);
    });

    MARK_UNUSABLE(f)
}

TEST_F(ConcurrencyTest, test_async) {
    auto f = concurrency::async(pool, []() -> int {
        return 42;
    });

    MARK_UNUSABLE(f)
}

TEST_F(ConcurrencyTest, test_async_single_then) {
    auto f = concurrency::async(pool, []() -> int {
        return 42;
    }).then(pool, [](concurrency::Result<int>&& result) -> int {
        int val = result.ValueOrThrow();
        EXPECT_EQ(val, 42);
    });

    MARK_UNUSABLE(f)
}

TEST_F(ConcurrencyTest, test_async_chain_then) {
    auto f = concurrency::async(pool, []() -> int {
        return 41;
    }).then(pool, [](concurrency::Result<int>&& result) -> int {
        int val = result.ValueOrThrow();
        EXPECT_EQ(val, 41);
        return val + 1;
    }).then(pool, [](concurrency::Result<int>&& result) -> int {
        int val = result.ValueOrThrow();
        EXPECT_EQ(val, 42);
    });

    MARK_UNUSABLE(f)
}

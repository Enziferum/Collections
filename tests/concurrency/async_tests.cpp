#include <gtest/gtest.h>
#include <joker/concurrency/future.hpp>
#include <joker/concurrency/thread_pool.hpp>

using namespace std::chrono_literals;

class ConcurrencyTest: public ::testing::Test {
public:
    void TearDown() override {
        pool.waitIdle();
        pool.join();
    }
protected:
    concurrency::thread_pool pool{4};
};

TEST_F(ConcurrencyTest, test_async_subscribe) {
    auto testId = std::this_thread::get_id();
    concurrency::async(pool, []() -> int {
        std::this_thread::sleep_for(1s);
        return 42;
    }).subscribe(pool, [testId](concurrency::Result<int> result) -> int {
        auto subscribeId = std::this_thread::get_id();
        EXPECT_EQ(testId, subscribeId);
        return result.get() + 1;
    });
}

//TEST_F(ConcurrencyTest, test_async) {
//    concurrency::async(pool, []() -> int {
//        return 42;
//    });
//}
//
//TEST_F(ConcurrencyTest, test_async_single_then) {
//    concurrency::async(pool, []() -> int {
//        return 42;
//    }).then(pool, [](concurrency::Result<int> result) -> int {
//        int val = result.get();
//        EXPECT_EQ(val, 42);
//        return val + 1;
//    });
//}
//
//TEST_F(ConcurrencyTest, test_async_chain_then) {
//    concurrency::async(pool, []() -> int {
//        return 42;
//    }).then(pool, [](concurrency::Result<int> result) -> int {
//        int val = result.get();
//        EXPECT_EQ(val, 42);
//        return val + 1;
//    }).then(pool, [](concurrency::Result<int> result) -> int {
//        int val = result.get();
//        EXPECT_EQ(val, 43);
//        return 0;
//    });
//}
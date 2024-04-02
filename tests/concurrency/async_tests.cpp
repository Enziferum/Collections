#include <gtest/gtest.h>
#include <collections/concurrency/future.hpp>
#include <collections/concurrency/thread_pool.hpp>

using namespace std::chrono_literals;

namespace {
    class ConcurrencyTest: public ::testing::Test {
    public:
        void TearDown() override {
            executor.waitIdle();
            executor.join();
        }
    protected:
        concurrency::thread_pool executor{4};
    };

    struct ThenRecoverException: public std::runtime_error {
        ThenRecoverException(std::string message): std::runtime_error(std::move(message)) {}
        const char* what() const noexcept override {
            return "ThenRecoverException";
        }
    };
}


TEST_F(ConcurrencyTest, test_async_subscribe) {
    auto testId = std::this_thread::get_id();

    auto f = concurrency::async(executor, []() -> int {
        std::this_thread::sleep_for(1s);
        return 41;
    }).subscribe(executor, [](concurrency::Result<int>&& result) {
        int value = result.ValueOrThrow() + 1;
        EXPECT_EQ(value, 42);
    });

    MARK_UNUSABLE(f)
}

TEST_F(ConcurrencyTest, test_async) {
    auto f = concurrency::async(executor, []() -> int {
        return 42;
    });

    MARK_UNUSABLE(f)
}

TEST_F(ConcurrencyTest, test_async_single_then) {
    auto f = concurrency::async(executor, []() -> int {
        return 42;
    }).then(executor, [](concurrency::Result<int>&& result) {
        int val = result.ValueOrThrow();
        EXPECT_EQ(val, 42);
    });

    MARK_UNUSABLE(f)
}

TEST_F(ConcurrencyTest, test_async_chain_then) {
    auto f = concurrency::async(executor, []() -> int {
        return 41;
    }).then(executor, [](concurrency::Result<int>&& result) -> int {
        int val = result.ValueOrThrow();
        EXPECT_EQ(val, 41);
        return val + 1;
    }).then(executor, [](concurrency::Result<int>&& result) {
        int val = result.ValueOrThrow();
        EXPECT_EQ(val, 42);
    });

    MARK_UNUSABLE(f)
}

TEST_F(ConcurrencyTest, test_async_then_recover_then) {
    auto first = []() {
        /// ... some code why we should throw exception
        throw ThenRecoverException("then_recover first throw");
        return 40;
    };
    /// handle possible exception
    auto second = [](concurrency::Result<int>&& result) {
        std::this_thread::sleep_for(1s);
        return 1;
    };
    auto third = [](concurrency::Result<int>&& result) {
        std::this_thread::sleep_for(1s);
        int val = result.ValueOrThrow() + 1;
        EXPECT_EQ(val, 2);
    };
    auto f = concurrency::async(executor, first)
            .recover(executor, second)
            .then(executor, third);

    MARK_UNUSABLE(f)
}
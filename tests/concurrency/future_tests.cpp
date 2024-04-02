#include <gtest/gtest.h>
#include <collections/concurrency/future.hpp>

namespace {
    struct TestA {
        TestA() = default;
        explicit TestA(int a): a{a} {}
        int a{0};
    };

    struct TestExecutor: concurrency::iexecutor {
        void execute(concurrency::Task task) override {
            // no-op //
        }
    };

    class ConcurrencyFutureMockTest: public ::testing::Test {
    protected:
        TestExecutor testExecutor;
    };
}



/*TEST_F(ConcurrencyFutureMockTest, base_promise_future_test) {
    concurrency::promise<TestA> promise;
    TestA testA{42};
    auto f = promise.get_future();
    promise.set_value(testExecutor, testA);
    auto v = f.get();
    EXPECT_EQ(v.a, 42);
}

TEST_F(ConcurrencyFutureMockTest, base_packeged_task_future_test) {
    concurrency::packaged_task<int(TestA)> task(testExecutor, [](TestA testA) {
        return 42;
    });
    TestA testA{42};
    auto f = task.get_future();

    auto v = f.get();
    EXPECT_EQ(v, 42);
}

TEST_F(ConcurrencyFutureMockTest, future_then_test) {
    concurrency::promise<TestA> promise;
    TestA testA{42};
    auto f = promise.get_future();
    promise.set_value(testExecutor, testA);
    std::move(f).then(testExecutor, [](decltype(f) f) {
        auto v = f.get();
        EXPECT_EQ(v.a, 42);
    });
}*/

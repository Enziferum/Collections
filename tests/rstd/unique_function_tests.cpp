#include <memory>
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <collections/rstl/unique_function.hpp>

namespace  {
    struct TestA {
        explicit TestA(int a): a{a}{}
        int a;
        virtual void f() {}
    };

    struct CopyOnlyTestA {
        explicit CopyOnlyTestA(int a): a{a}{}
        CopyOnlyTestA(const CopyOnlyTestA& other) = default;
        CopyOnlyTestA& operator=(const CopyOnlyTestA& other) = default;
        CopyOnlyTestA(CopyOnlyTestA&& other) = delete;
        CopyOnlyTestA& operator=(CopyOnlyTestA&& other) = delete;

        int a;
    };

    int sum_func(TestA a) {
        ++a.a;
        return a.a;
    }

    struct TestAFunctor {
        int operator()(TestA testA) {
            ++testA.a;
            return testA.a;
        }
    };
}

TEST(Rstl, unique_function_void_return_test) {
    rstl::unique_function<void()> f = []() {
        int a = 2;
        ++a;
        EXPECT_EQ(a, 3);
    };

    f();
}

TEST(Rstl, unique_function_value_return_test) {
    rstl::unique_function<int()> f = []() -> int {
        int a = 42;
        ++a;
        return a;
    };

    EXPECT_EQ(f(), 43);
}

TEST(Rstl, unique_function_void_return_input_value_test) {
    rstl::unique_function<void(int)> f = [](int a){
        ++a;
        EXPECT_EQ(a, 43);
    };
}

TEST(Rstl, unique_function_value_return_input_value_test) {
    rstl::unique_function<int(int)> f = [](int a) {
        ++a;
        return a;
    };
    EXPECT_EQ(f(42), 43);
}

TEST(Rstl, move_unique_function_void_return_input_value_test) {
    rstl::unique_function<void(int)> f = [](int a) {
        ++a;
        EXPECT_EQ(a, 43);
    };

    auto f1 = std::move(f);
    f1(42);
}

TEST(Rstl, move_unique_function_value_return_input_value_test) {
    rstl::unique_function<int(int)> f = [](int a) {
        ++a;
        return a;
    };

    auto f1 = std::move(f);
    EXPECT_EQ(f1(42), 43);
}

TEST(Rstl, unique_function_value_return_input_value_custom_type_test) {
    struct A {
        int a;
    };

    rstl::unique_function<int(A)> f = [](A a) {
        ++a.a;
        return a.a;
    };

    A test_a{42};
    EXPECT_EQ(f(std::move(test_a)), 43);
}

TEST(Rstl, move_unique_function_value_return_input_value_custom_type_non_pod_type_test) {
    struct A {
        explicit A(int a): a{a}{}
        int a;
        virtual void f() {}
    };

    rstl::unique_function<int(A)> f = [](A a) {
        ++a.a;
        return a.a;
    };

    A test_a{42};
    auto f1 = std::move(f);
    EXPECT_EQ(f1(std::move(test_a)), 43);
}

TEST(Rstl, unique_function_value_return_input_value_custom_type_standard_function_test) {
    rstl::unique_function<int(TestA)> f = &sum_func;

    TestA test_a{42};
    EXPECT_EQ(f(std::move(test_a)), 43);
}

TEST(Rstl, move_unique_function_value_return_input_value_custom_type_standard_function_test) {
    rstl::unique_function<int(TestA)> f = &sum_func;

    TestA test_a{42};
    auto f1 = std::move(f);
    EXPECT_EQ(f1(std::move(test_a)), 43);
}

TEST(Rstl, unique_function_value_return_input_value_custom_type_functor_test) {
    rstl::unique_function<int(TestA)> f = TestAFunctor();

    TestA test_a{42};
    EXPECT_EQ(f(std::move(test_a)), 43);
}

TEST(Rstl, move_unique_function_value_return_input_value_custom_type_functor_test) {
    rstl::unique_function<int(TestA)> f = TestAFunctor();

    TestA test_a{42};
    auto f1 = std::move(f);
    EXPECT_EQ(f1(std::move(test_a)), 43);
}

TEST(Rstl, unique_function_capture_move_only_test) {
    std::unique_ptr<int> u_ptr = std::make_unique<int>(42);

    rstl::unique_function<void()> f = [u_ptr = std::move(u_ptr)]() {
        (*u_ptr)++;
        EXPECT_EQ(*u_ptr, 43);
    };

    f();
}

TEST(Rstl, move_unique_function_capture_move_only_test) {
    std::unique_ptr<int> u_ptr = std::make_unique<int>(42);

    rstl::unique_function<void()> f = [u_ptr = std::move(u_ptr)]() {
        (*u_ptr)++;
        EXPECT_EQ(*u_ptr, 43);
    };

    auto f1 = std::move(f);
    f1();
}

TEST(Rstl, unique_function_throw_no_callbable_test) {
    rstl::unique_function<void()> f;
    EXPECT_THAT([&f]() { f(); },
                testing::ThrowsMessage<rstl::unique_function_bad_callable>(
                        testing::HasSubstr("rstd::unique_function, create callable before invoke")));
}
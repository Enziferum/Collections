#include <collections/concurrency/thread_pool.hpp>
#include <collections/concurrency/future.hpp>

#include <iostream>
using namespace std::chrono_literals;

void single_subscribe(concurrency::iexecutor& executor) {
    auto first = []() {
        std::cout << "first done!" << std::endl;
        return 42;
    };
    auto f = concurrency::async(executor, first)
            .subscribe(executor, [](concurrency::Result<int>&& result) {
                std::cout << "Subscribe result " << result.ValueOrThrow() << std::endl;
            });
    /// Idea that future can't be using somewhere else, because we work with subscribe / then but not get() method directly.
    MARK_UNUSABLE(f)
}

void many_then(concurrency::iexecutor& executor) {
    auto first = []() {
        //std::this_thread::sleep_for(1s);
        std::cout << "first done!" << std::endl;
        return 40;
    };
    auto second = [](concurrency::Result<int>&& result) {
        //std::this_thread::sleep_for(1s);
        std::cout << "second done!" << std::endl;
        return result.ValueOrThrow() + 1;
    };
    auto third = [](concurrency::Result<int>&& result) {
        //std::this_thread::sleep_for(1s);
        std::cout << "third finally print result: " << result.ValueOrThrow() + 1 << std::endl;
    };
    auto f = concurrency::async(executor, first)
            .then(executor, second)
            .then(executor, third);

    MARK_UNUSABLE(f)
}

struct ThenRecoverException: public std::runtime_error {
    ThenRecoverException(std::string message): std::runtime_error(std::move(message)) {}
    const char* what() const noexcept override {
        return "ThenRecoverException";
    }
};

void then_recover(concurrency::iexecutor& executor) {
    auto first = []() {
        // std::this_thread::sleep_for(1s);
        std::cout << "first done!" << std::endl;
        /// ... some code why we should throw exception
        throw ThenRecoverException("then_recover first throw");
        return 40;
    };
    /// handle possible exception
    auto second = [](concurrency::Result<int>&& result) {
        //std::this_thread::sleep_for(1s);
        std::cout << "second process!" << std::endl;
        return 1;
    };
    auto third = [](concurrency::Result<int>&& result) {
        //std::this_thread::sleep_for(1s);
        std::cout << "third finally print result: " << result.ValueOrThrow() + 1 << std::endl;
    };
    auto f = concurrency::async(executor, first)
            .recover(executor, second)
            .then(executor, third);

    MARK_UNUSABLE(f)
}

void run() {
    concurrency::thread_pool pool{4};

    single_subscribe(pool);
    many_then(pool);
    then_recover(pool);
    std::cout << "Continue our work!" << std::endl;

    pool.waitIdle();
}

int main () {
    run();
    return 0;
}
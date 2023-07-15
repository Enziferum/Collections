#include <joker/concurrency/thread_pool.hpp>
#include <joker/concurrency/future.hpp>
#include <iostream>

int main () {
    using namespace std::chrono_literals;
    concurrency::thread_pool pool{4};

    auto first = []() {
        std::this_thread::sleep_for(1s);
        return 42;
    };

    auto second = [](concurrency::Result<int> result) {
        return result.ValueOrThrow() + 1;
    };

    auto third = [](concurrency::Result<int> result) {
      //  throw std::runtime_error("my error");
        std::cout << "result = " << result.ValueOrThrow() << "\n";
        return std::to_string(result.ValueOrThrow() * 2);
    };

    auto four = [](concurrency::Result<std::string> result) {
        std::cout << "Final result = " << result.ValueOrThrow() << "\n";
        return 0;
    };

//    .recover(pool, [](std::exception_ptr) {
//        return std::string("Ni hao");
//    })
//
    concurrency::async(pool, first)
        .then(pool, second)
        .then(pool, third)
        .then(pool, four);


    pool.waitIdle();
    pool.join();

    return 0;
}
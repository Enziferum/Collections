#include <iostream>
#include <thread>
#include "benchmark.hpp"

void bench_test() {
        using namespace std::chrono_literals;
    util::BenchmarkManager benchmarkManager;

    util::Benchmark benchmark{"Sample 2", []() {
        std::this_thread::sleep_for(10ms);
    }};

    benchmarkManager.do_benchmark("Sample 1", []() {});

    benchmarkManager.do_benchmark(std::move(benchmark), 5);

    auto res = benchmarkManager[1];
    std::cout << res.time.asMilliseconds() << "\n";
}

int main() {
    bench_test();
    return 0;
}

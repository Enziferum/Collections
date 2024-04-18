#include <algorithm>
#include <execution>
#include <iostream>

#include "benchmark.hpp"

namespace util {
    Time operator+(const Time& left, const Time& right) {
        return Time{left.count + right.count};
    }

    bool operator<(const Time& left, const Time& right) {
        return left.count < right.count;
    }


    std::ostream& operator << (std::ostream& os, const Time& time) {
        os << "Time in mics: " << time.count << "\n";
        return os;
    }

    BenchmarkResult operator+(const BenchmarkResult& left, const BenchmarkResult& right) {
        return BenchmarkResult{left.time + right.time};
    }

    bool operator<(const BenchmarkResult& left, const BenchmarkResult& right) {
        return left.time < right.time;
    }

    void BenchmarkManager::do_benchmark(std::string&& name, benchmarkCallback&& callback) {
        Benchmark benchmark{std::move(name), std::move(callback)};
        do_benchmark(std::move(benchmark));
    }

    void BenchmarkManager::do_benchmark(Benchmark&& benchmark) {
        std::cout << "Start " << benchmark.m_message << "\n";
        // benchmark();
        std::cout << "Stop " << benchmark.m_message << "\n";

        m_benchmark_results.emplace_back(benchmark.getResult());
    }

    const BenchmarkResult& BenchmarkManager::do_benchmark(Benchmark&& benchmark, unsigned int tryCount) {
        std::vector<BenchmarkResult> tmpResult{tryCount};
        std::cout << "Start " << benchmark.m_message << ". Repeat " << tryCount << " times" << "\n";

        for(int i = 0; i < tryCount; ++i) {
            //benchmark();
            tmpResult[i] = benchmark.getResult();
            // std::cout << tmpResult[i].time.asMilliseconds() << std::endl;
        }
        std::cout << "Stop" << benchmark.m_message << "\n";
        auto result = std::reduce(tmpResult.begin(), tmpResult.end());
        result.time.count /= static_cast<int>(tryCount);
        return m_benchmark_results.emplace_back(result);
    }
}
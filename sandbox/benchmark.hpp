#pragma once
#include <chrono>
#include <functional>
#include <string>
#include <vector>
#include <stdexcept>

namespace util {
    using benchmarkCallback = std::function<void()>;

    enum class benchmark_throw_type {
        no_callback
    };

    inline std::string make_benchmark_error_code(benchmark_throw_type type) {
        switch(type){
            case benchmark_throw_type::no_callback:
                return "benchmark_throw_type::no_callback";
        }
    }

    class BenchmarkException: public std::logic_error {
    public:
        explicit BenchmarkException(benchmark_throw_type type):
            BenchmarkException(make_benchmark_error_code(type)) {}
        using std::logic_error::what;
    private:
        explicit BenchmarkException(std::string&& message): std::logic_error("Benchmark:" + message){}
    };

    struct Time {

        Time() = default;
        Time(const Time& other) = default;
        explicit Time(std::int64_t other): count(other) {}

        Time& operator=(std::int64_t c) {
            count = c;
            return *this;
        }

        [[nodiscard]]
        float asSeconds() const {
            return (static_cast<float>(count) / 1000000.f);
        }

        [[nodiscard]]
        float asMilliseconds() const {
            return (static_cast<float>(count) / 1000.f);
        };

        friend Time operator+(const Time& left, const Time& right);
        friend std::ostream& operator << (std::ostream& os, const Time& time);

        std::int64_t count;
    };


    struct BenchmarkResult {
        Time time;
        friend BenchmarkResult operator+(const BenchmarkResult& left, const BenchmarkResult& right);
    };

    class Benchmark {
    public:
        Benchmark(std::string&& message, benchmarkCallback&& callback):
            m_message(std::move(message)), m_callback(std::move(callback)) {}
        ~Benchmark() = default;

        void operator()() {
            if(!m_callback)
                throw BenchmarkException(benchmark_throw_type::no_callback);
            auto start = std::chrono::high_resolution_clock::now();
            m_callback();
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = end - start;
            std::int64_t count = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
            m_result.time = Time(count);
        }

        [[nodiscard]]
        BenchmarkResult getResult() const {
            return m_result;
        }
    public:
        std::string m_message;
        benchmarkCallback m_callback;
        BenchmarkResult m_result{};
    };

    class BenchmarkManager {
    public:
        BenchmarkManager() = default;
        BenchmarkManager(const BenchmarkManager& other) = delete;
        BenchmarkManager& operator=(const BenchmarkManager& other) = delete;
        BenchmarkManager(BenchmarkManager&& other) = delete;
        BenchmarkManager& operator=(BenchmarkManager&& other) = delete;
        ~BenchmarkManager() = default;

        /// \brief one shot benchmark
        void do_benchmark(std::string&& name, benchmarkCallback&& callback);

        /// \brief one shot benchmark
        void do_benchmark(Benchmark&& benchmark);

        /// \brief n shot benchmark, save as result's time count median value
        void do_benchmark(Benchmark&& benchmark, unsigned int tryCount);


        const BenchmarkResult& operator[](std::size_t index) {
            return m_benchmark_results[index];
        }

        [[nodiscard]]
        const std::vector<BenchmarkResult>& getResults() const { return m_benchmark_results; }
    private:
        std::vector<BenchmarkResult> m_benchmark_results;
    };

}
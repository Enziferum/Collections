#pragma once
#include <collections/rstd/unique_function.hpp>

namespace collections::concurrency {

    using Task = rstd::unique_function<void()>;

    /// TODO(a.raag): add Statictics( num of tasks done/processing )
    struct ExecutorStatistics {
        std::size_t numDoneTasks;
        std::size_t numProcessingTasks;
    };

    class iexecutor {
    public:
        virtual ~iexecutor() = 0;
        virtual void execute(Task task) = 0;

        [[nodiscard]]
        const ExecutorStatistics& getStatistics() const { return m_stats; }
    protected:
        ExecutorStatistics m_stats{};
    };
} // namespace collections::rstd
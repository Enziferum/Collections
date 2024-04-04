#pragma once
#include <mutex>
#include <condition_variable>

namespace concurrency {

    class TaskCount {
    public:
        TaskCount() = default;
        TaskCount(const TaskCount&& other) = delete;
        TaskCount& operator=(const TaskCount&& other) = delete;
        TaskCount(TaskCount&& other) = delete;
        TaskCount& operator=(TaskCount&& other) = delete;
        ~TaskCount() = default;


        inline void Add(size_t count = 1) {
            m_count.fetch_add(count, std::memory_order_relaxed);
        }

        inline void Done() {
            if (m_count.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard guard(m_mutex);
                m_idle.notify_all();
            }
        }

        /// \brief Multi-shot
        inline void WaitIdle() {
            std::unique_lock lock(m_mutex);
            while (m_count.load() > 0) {
                m_idle.wait(lock);
            }
        }

    private:
        atomic<size_t> m_count{0};
        mutex m_mutex;
        condition_variable m_idle;
    };
}
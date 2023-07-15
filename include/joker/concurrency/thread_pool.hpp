#pragma once
#include <vector>
#include <memory>

#include <joker/queue/dummy_threadsafe_queue.hpp>
#include <joker/queue/threadsafe_queue.hpp>
#include <joker/queue/work_stealing_queue.hpp>

#include "iexecutor.hpp"
#include "twist_wrapper.hpp"
#include "future.hpp"

namespace concurrency {

    class TaskCount {
        public:
            void Add(size_t count = 1) {
                count_.fetch_add(count, std::memory_order_relaxed);
            }

            void Done() {
                if (count_.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                    std::lock_guard guard(mutex_);
                    idle_.notify_all();
                }
            }

            // Multi-shot
            void WaitIdle() {
                std::unique_lock lock(mutex_);
                while (count_.load() > 0) {
                    idle_.wait(lock);
                }
            }

        private:
        atomic<size_t> count_{0};
        mutex mutex_;
        condition_variable idle_;
    };


    class thread_pool: public iexecutor {
    public:
        explicit thread_pool(unsigned int threadCount = 4);
        thread_pool(const thread_pool& other) = delete;
        thread_pool& operator=(const thread_pool& other) = delete;
        thread_pool(thread_pool&& other) = delete;
        thread_pool& operator=(thread_pool&& other) = delete;
        ~thread_pool() override;

        template<typename T, typename ... Args>
        future<T> execute(Task task, Args&& ... args);
        void execute(Task task) override;

        void join();
        void waitIdle();
        std::size_t tasksValue();
    private:
        void thread_function();
    private:
        std::vector<std::thread> m_threads;
        std::vector<std::unique_ptr<work_stealing_queue>> m_stealQueues;

        net::dummy_threadsafe_queue<Task> m_globalQueue;
        static thread_local work_stealing_queue* m_localQueue;

        std::atomic_bool m_done;
        TaskCount m_taskCount;
    };


    template<typename T, typename ... Args>
    future<T> thread_pool::execute(Task task, Args&& ... args) {
        return async(*this, std::move(task), std::forward<Args>(args)...);
    }

} // namespace concurrency
#pragma once
#include <vector>
#include <memory>

#include <collections/queue/dummy_threadsafe_queue.hpp>
#include <collections/queue/threadsafe_queue.hpp>
#include <collections/queue/work_stealing_queue.hpp>

#include "iexecutor.hpp"
#include "twist_wrapper.hpp"
#include "future.hpp"
#include "task_count.hpp"

namespace concurrency {

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
        void thread_function(unsigned threadIndex);
        void run_pending_tasks();

        bool pop_task_local(Task& task);
        bool pop_task_global(Task& task);
        bool pop_task_other_thread(Task& task);
    private:
        std::vector<std::thread> m_threads;
        using TaskStealingQueue = concurrency::work_stealing_queue<Task>;
        concurrency::dummy_threadsafe_queue<Task> m_globalQueue;

        std::vector<std::unique_ptr<TaskStealingQueue>> m_stealQueues;
        static thread_local TaskStealingQueue* m_localQueue;
        static thread_local unsigned m_threadIndex;

        std::atomic_bool m_done;
        TaskCount m_taskCount;
    };


    template<typename T, typename ... Args>
    future<T> thread_pool::execute(Task task, Args&& ... args) {
        return async(*this, std::move(task), std::forward<Args>(args)...);
    }

} // namespace concurrency
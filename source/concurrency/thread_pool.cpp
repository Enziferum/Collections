#include <collections/concurrency/thread_pool.hpp>
#include <iostream>

namespace collections::concurrency {

    thread_local unsigned thread_pool::m_threadIndex = 0;
    thread_local thread_pool::TaskStealingQueue* thread_pool::m_localQueue = nullptr;


    thread_pool::thread_pool(unsigned int threadCount) {
        try {
            for(int i = 0; i < threadCount; ++i)
                m_stealQueues.push_back(std::make_unique<TaskStealingQueue>());

            for(int i = 0; i < threadCount; ++i)
                m_threads.emplace_back(&thread_pool::thread_function, this, i);

            m_done.store(false, std::memory_order::memory_order_relaxed);
        }
        catch(...) {
            m_done.store(true, std::memory_order::memory_order_relaxed);
        }
    }

    thread_pool::~thread_pool() {
        join();
    }


    void thread_pool::join() {
        m_done.store(true, std::memory_order::memory_order_relaxed);
        for(auto& thread: m_threads) {
            if(thread.joinable())
                thread.join();
        }
    }

    void thread_pool::execute(Task task) {
        m_taskCount.Add();
        if(m_localQueue)
            m_localQueue -> push(std::move(task));
        else
            m_globalQueue.push(std::move(task));
    }

    std::size_t thread_pool::tasksValue()  {
        return m_globalQueue.size();
    }

    void thread_pool::waitIdle() {
       m_taskCount.WaitIdle();
    }

    void thread_pool::thread_function(unsigned int threadIndex) {
        m_threadIndex = threadIndex;
        m_localQueue = m_stealQueues[m_threadIndex].get();

        while(!m_done.load(std::memory_order::memory_order_relaxed))
            run_pending_tasks();
    }

    bool thread_pool::pop_task_local(Task& task) {
        return m_localQueue && m_localQueue -> try_pop(task);
    }

    bool thread_pool::pop_task_global(Task& task) {
        return m_globalQueue.try_pop(task);
    }

    bool thread_pool::pop_task_other_thread(Task& task) {
        for(unsigned i = 0; i < m_stealQueues.size(); ++i) {
            unsigned const index = (m_threadIndex + i + 1) % m_stealQueues.size();
            if(m_stealQueues[index] -> try_steal(task))
                return true;
        }
        return false;
    }

    void thread_pool::run_pending_tasks() {
        Task currentTask;

        if(
            pop_task_local(currentTask) ||
            pop_task_global(currentTask) ||
            pop_task_other_thread(currentTask)
        ) {
            currentTask();
            m_taskCount.Done();
        }
        else {
            std::this_thread::yield();
        }
    }


} // namespace collections::concurrency
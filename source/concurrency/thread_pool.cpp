#include <joker/concurrency/thread_pool.hpp>

namespace concurrency {

    thread_pool::thread_pool(unsigned int threadCount) {
        try {
            for(int i = 0; i < threadCount; ++i) {
                m_threads.emplace_back(&thread_pool::thread_function, this);
            }

            m_done.store(false, std::memory_order::memory_order_relaxed);
        }
        catch(...) {
            m_done.store(true, std::memory_order::memory_order_relaxed);
        }
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
        m_globalQueue.push(std::move(task));
    }

    std::size_t thread_pool::tasksValue()  {
        return m_globalQueue.size();
    }

    void thread_pool::thread_function() {
        while(!m_done.load(std::memory_order::memory_order_relaxed)) {
            std::shared_ptr<Task> currentTask{nullptr};
            currentTask = std::move(m_globalQueue.try_pop());
            if(currentTask) {
                currentTask -> operator()();
                m_taskCount.Done();
            }
            else {
                std::this_thread::yield();
            }
        }
    }

    thread_pool::~thread_pool() {
       join();
    }

    void thread_pool::waitIdle() {
       m_taskCount.WaitIdle();
    }


} // namespace concurrency
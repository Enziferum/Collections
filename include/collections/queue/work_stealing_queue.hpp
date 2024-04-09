#pragma once
#include <deque>
#include <memory>

namespace collections::concurrency {

    template<typename T>
    class work_stealing_queue {
    private:
        struct Node {
            Node* next;
            Node* prev;
        };
    public:
        work_stealing_queue() = default;
        work_stealing_queue(const work_stealing_queue& other) = delete;
        work_stealing_queue& operator=(const work_stealing_queue& other) = delete;
        work_stealing_queue(work_stealing_queue&& other) = delete;
        work_stealing_queue& operator=(work_stealing_queue&& other) = delete;
        ~work_stealing_queue() = default;

        void push(T&& task) {
            std::lock_guard<std::mutex> lockGuard{m_mutex};
            m_deque.push_front(std::move(task));
        }

        bool empty() const {
            std::lock_guard<std::mutex> lockGuard{m_mutex};
            return m_deque.empty();
        }

        bool try_pop(T& task) {
            std::lock_guard<std::mutex> lockGuard{m_mutex};
            if(m_deque.empty())
                return false;

            task = std::move(m_deque.front());
            m_deque.pop_front();

            return true;
        }

        bool try_steal(T& task) {
            std::lock_guard<std::mutex> lockGuard{m_mutex};
            if(m_deque.empty())
                return false;
            task = std::move(m_deque.back());
            m_deque.pop_back();
            return true;
        }

    private:
        std::deque<T> m_deque;
        mutable std::mutex m_mutex;
    };

} // namespace collections::concurrency
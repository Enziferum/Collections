#pragma once

#include <queue>
#include <memory>
#include <mutex>

namespace concurrency {

    /// \brief Dummy Realization ThreadSafe Queue based on mutex and std::queue
    template<typename T>
    class dummy_threadsafe_queue {
    public:
        dummy_threadsafe_queue() = default;
        dummy_threadsafe_queue(const dummy_threadsafe_queue& other) = delete;
        dummy_threadsafe_queue& operator=(const dummy_threadsafe_queue& other) = delete;
        dummy_threadsafe_queue(dummy_threadsafe_queue&& other) = delete;
        dummy_threadsafe_queue& operator=(dummy_threadsafe_queue&& other) = delete;
        ~dummy_threadsafe_queue() noexcept = default;

        void push(const T& value) {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_queue.push(value);
        }

        void push(T&& value) {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            m_queue.push(std::move(value));
        }

        bool try_pop(T& value) {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            value = std::move(m_queue.front());
            m_queue.pop();
            return true;
        }

        bool wait_pop(T& value) {
            value = m_queue.front();
            m_queue.pop();
            return true;
        }

        std::shared_ptr<T> try_pop() {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if(m_queue.empty())
                return nullptr;
            auto value = std::move(m_queue.front());
            m_queue.pop();
            return std::make_shared<T>(std::move(value));
        }

        std::shared_ptr<T> wait_pop() {
            auto&& value = m_queue.front();
            m_queue.pop();
            return std::make_shared<T>(std::move(value));
        }

        std::size_t size() {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            return m_queue.size();
        }

        bool empty() {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            return m_queue.empty();
        }
    private:
        std::queue<T> m_queue;
        std::mutex m_queueMutex;
    };
}
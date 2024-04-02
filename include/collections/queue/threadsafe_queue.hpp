#pragma once

#include <memory>
#include <mutex>

namespace net {
    template<typename T>
    class threadsafe_queue {
    private:
        struct Node {
            Node* next;
            T value;
        };

    public:
        threadsafe_queue() = default;
        threadsafe_queue(const threadsafe_queue& other) = delete;
        threadsafe_queue& operator=(const threadsafe_queue& other) = delete;
        threadsafe_queue(threadsafe_queue&& other) = delete;
        threadsafe_queue& operator=(threadsafe_queue&& other) = delete;
        ~threadsafe_queue() noexcept = default;


        void push(const T& value) {

        }

        void push(T&& value) {

        }


        bool try_pop(T& value) {

        }

        std::shared_ptr<T> try_pop() {

        }


        bool wait_pop(T& value) {

        }

        std::shared_ptr<T> wait_pop() {

        }


        bool empty() const {}
    private:

        Node* getTail() {}
    private:
        std::mutex m_headMutex;
        std::mutex m_tailMutex;
        Node* m_head;
        Node* m_tail;
    };
}
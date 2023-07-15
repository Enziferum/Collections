#pragma once

namespace concurrency {
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

        void push();
        bool try_pop();
        bool try_steal();
    };
}
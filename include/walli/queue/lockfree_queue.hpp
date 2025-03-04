#pragma once
#include <memory>
#include "hazard_pointer.hpp"

namespace collections::concurrency {

    /// \brief Guarding Allocations (base usage if construct throw exception deallocate and some catches exception)
    template<typename Allocator>
    class AllocGuard {
    private:
        using alloc_traits = std::allocator_traits<Allocator>;
        using pointer = typename alloc_traits::pointer;
    public:
        AllocGuard(Allocator& allocator, pointer memPointer) noexcept:
                m_allocator(std::addressof(allocator)), m_pointer{memPointer} {}

        AllocGuard(AllocGuard&& other) noexcept: m_allocator(other.m_allocator), m_pointer(other.m_pointer) {
            other.m_pointer = nullptr;
        }

        AllocGuard(const AllocGuard& other) = delete;
        AllocGuard& operator=(const AllocGuard& other) = delete;
        AllocGuard& operator=(AllocGuard&& other) = delete;

        AllocGuard& operator=(std::nullptr_t) noexcept {
            m_pointer = nullptr;
            return *this;
        }

        ~AllocGuard()  {
            if(m_pointer)
                alloc_traits::deallocate(*m_allocator, m_pointer, 1);
        }

    private:
        Allocator* m_allocator;
        pointer m_pointer;
    };


    template<typename T, typename Allocator = std::allocator<T>, int cache_lineSz = 64>
    class ms_queue {
    private:

        struct Node {
            explicit Node(T value): value(std::move(value)){}

            std::atomic<Node*> next{nullptr};
            T value{};
        };

        using t_alloc_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
        using t_alloc_traits = std::allocator_traits<t_alloc_type>;

        using node_alloc_type = typename t_alloc_traits::template rebind_alloc<Node>;
        using node_alloc_traits = std::allocator_traits<node_alloc_type>;

        /// \brief Garbage collector for deallocate nodes
        using GC = GC::defaultHazardPointer;
    public:
        explicit ms_queue(const Allocator& alloc = Allocator()): m_alloc(alloc){}
        ms_queue(const ms_queue& other) = delete;
        ms_queue& operator=(const ms_queue& other) = delete;
        ms_queue(ms_queue&& other) = delete;
        ms_queue& operator=(ms_queue&& other) = delete;
        ~ms_queue() {

            m_head.store(nullptr, std::memory_order::memory_order_relaxed);
            m_tail.store(nullptr, std::memory_order::memory_order_relaxed);
            dispose_node(m_head);
        }


        void push(T&& value) {
            /// allocate
            Node* newNode = allocateNode(std::move(value));
            GC::Guard guard;

            Node* tmpTail;
            while(true) {
                tmpTail = guard.protect(m_tail); /// TODO(a.raag) protect through hazard pointer guard

                Node* next = tmpTail -> next.load(std::memory_order::memory_order_acquire);
                if(next != nullptr) {
                    /// helper
                    m_tail.compare_exchange_weak(tmpTail, next,
                                                 std::memory_order::memory_order_release, /// success
                                                 std::memory_order::memory_order_relaxed); /// failure
                    continue;
                }

                Node* tmp = nullptr;
                if(tmpTail -> next.compare_exchange_strong(tmp, newNode, std::memory_order::memory_order_relaxed))
                    break;

                /// back_off strategy is none now
            }

            if(!m_tail.compare_exchange_strong(tmpTail, newNode,
                                               std::memory_order::memory_order_release, /// success
                                               std::memory_order::memory_order_relaxed)) /// failure
            {
                /// stats error
            }

            m_itemCntr.fetch_add(1, std::memory_order::memory_order_seq_cst);
        }

        struct pop_result {
            GC::GuardArray<2> guards;
            Node* head;
            Node* next;
        };

        bool try_pop(T& value) {
            pop_result res;
            if(do_try_pop(res)) {
                dispose_result(res);
                value = std::move(res.next -> value);
                return true;
            }

            return false;
        }

        [[nodiscard]] bool empty() const noexcept {
            /// TODO(a.raag): check m_head -> next != nullptr due to we have always dummy node
        }

        [[nodiscard]] std::size_t size() const noexcept {
            return m_itemCntr.load(std::memory_order::memory_order_seq_cst);
        }
    private:
        bool do_try_pop(pop_result& result) {
            Node* next;
            Node* tmpHead;

            while(true) {
                tmpHead = result.guards.protect(0, m_head);/// protect tmpHead
                next = result.guards.protect(1, tmpHead -> next); /// protect next

                if(m_head.load(std::memory_order::memory_order_acquire) != tmpHead)
                    continue;

                if(next == nullptr)
                    return false;

                Node* tmpTail = m_tail.load(std::memory_order::memory_order_acquire);
                if(tmpHead == tmpTail) {
                    /// helper to push operation
                    m_tail.compare_exchange_strong(tmpHead, next,
                                                   std::memory_order::memory_order_release,
                                                   std::memory_order::memory_order_relaxed);
                    continue;
                }

                if(m_head.compare_exchange_strong(tmpHead, next,
                                                  std::memory_order::memory_order_acquire,
                                                  std::memory_order::memory_order_relaxed))
                    break;

            }
            m_itemCntr.fetch_sub(1, std::memory_order::memory_order_seq_cst); // ? not so strong mem order
            result.head = tmpHead;
            result.next = next;
            return true;
        }

        void dispose_result(pop_result& result) {
            dispose_node(result.head);
        }

        void dispose_node(Node* node) {
            if(node) {
                /// TODO(a.raag): check if ok capture this??
                static auto removeCallback = [this](void* pointer) mutable -> void{
                    /// assert(pointer != nullptr);
                    ms_queue::clear_links(static_cast<Node*>(pointer));
                    node_alloc_traits::destroy(m_alloc, static_cast<Node*>(pointer));
                    node_alloc_traits::deallocate(m_alloc, static_cast<Node*>(pointer), 1);
                };

                // GC::retire(node, removeCallback);
            }
        }

        static void clear_links(Node* node) {
            node -> next.store(nullptr, std::memory_order::memory_order_release);
        }

        template<typename ... Args>
        Node* allocateNode(Args&& ... args) {
            Node* _allocNode = node_alloc_traits::allocate(m_alloc, 1);
            AllocGuard<node_alloc_type> allocGuard{m_alloc, _allocNode};
            node_alloc_traits::construct(m_alloc, _allocNode, std::forward<Args>(args)...);
            allocGuard = nullptr;
            return _allocNode;
        }

    private:
        alignas(cache_lineSz) std::atomic<Node*> m_head { nullptr };
        alignas(cache_lineSz) std::atomic<Node*> m_tail { nullptr };
        std::atomic<unsigned> m_itemCntr { 0 };
        node_alloc_type m_alloc;
    };



    class GarbageCollector {
    private:

        struct ThreadGarbageGuardian {
            ~ThreadGarbageGuardian() {
                /// TODO(a.raag): destroy thread_data
                GC::details::SMR::detach_thread();
            }
        };

    public:
        static ThreadGarbageGuardian attach_thread();


        GarbageCollector();
        ~GarbageCollector();
    private:
        struct TLD {
            static thread_local int* threadData;
        };
    private:
        using HP = GC::defaultHazardPointer;
        HP m_garbageCollector;
    };


    GarbageCollector::GarbageCollector() {

    }

    GarbageCollector::~GarbageCollector() {

    }

    GarbageCollector::ThreadGarbageGuardian GarbageCollector::attach_thread() {
        /// TODO(a.raag): init thread_data
        GC::details::SMR::attach_thread();
        return {};
    }




}

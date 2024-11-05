#pragma once
#include <memory>
#include <mutex>
#include <thread>

#include "collections/rstd/spinlock.hpp"


namespace collections::concurrency {

    template<typename T>
    class intrusive_blocking_threadsafe_queue {
        std::allocator_traits<T> a;
    };

    template<bool CanMove>
    struct uninitialized_shift{};

    template<>
    struct uninitialized_shift<false> {
        template<typename ForwardIt>
        static void shift(ForwardIt begin, ForwardIt end) {
            using T = typename ForwardIt::value_type;
            ForwardIt first = begin;
            ForwardIt current = begin;
            try {
                for(; begin != end; ++begin, void(current))
                    ::new (static_cast<void*>(std::addressof(*current))) T(*begin);
            }
            catch(...) {
                for (; first != begin; ++first)
                    first->~T();
                throw;
            }
        }
    };

    template<>
    struct uninitialized_shift<true> {
        template<typename InputIt, typename NoThrowForwardIt>
        static NoThrowForwardIt shift(InputIt begin, InputIt end, NoThrowForwardIt d_first) {
            using T = typename NoThrowForwardIt::value_type;
            NoThrowForwardIt current = d_first;
            try {
                for(; begin != end; ++begin, (void)++current)
                    ::new (static_cast<void*>(std::addressof(*current))) T(std::move(*begin));
                return current;
            }
            catch(...) {
                for (; d_first != current; ++d_first)
                    d_first->~T();
                throw;
            }
        }
    };

    class CallStack {
    private:
    };


    template<typename Mutex = std::mutex>
    class hierarchical_mutex {
    public:
        explicit hierarchical_mutex(ulong value): m_priority(value), m_prevPriority(0) {}
        hierarchical_mutex(const hierarchical_mutex& other) = delete;
        hierarchical_mutex& operator=(const hierarchical_mutex& other) = delete;
        ~hierarchical_mutex() = default;

        inline void lock() {
            check_for_hierarchy_violation();
            m_internalMutex.lock();
            update_hierarchy_value();
        }

        inline bool try_lock() noexcept {
            check_for_hierarchy_violation();
            if(!m_internalMutex.try_lock())
                return false;
            update_hierarchy_value();
            return true;
        }

        inline void unlock() {
            if(this_thread_priority != m_priority)
                throw std::logic_error("mutex hierarchy");
            this_thread_priority = m_prevPriority;
            m_internalMutex.unlock();
        }
    private:
        void check_for_hierarchy_violation() {
            if(this_thread_priority <= m_priority)
                throw std::logic_error("mutex hierarchy");
        }

        void update_hierarchy_value() {
            m_prevPriority = this_thread_priority;
            this_thread_priority = m_priority;
        }
    private:
        unsigned long m_priority;
        unsigned long m_prevPriority;
        Mutex m_internalMutex;
        static thread_local unsigned long this_thread_priority;
    };

    template<typename Func, typename TupleT, std::size_t... Is>
    void for_each_helper(Func&& func, TupleT&& tuple, std::index_sequence<Is...>) {
        (func(std::get<Is>(std::forward<TupleT>(tuple))), ...);
    }

    template <std::size_t ... Is>
    constexpr auto indexSequenceReverse (std::index_sequence<Is...> const &)
    -> decltype( std::index_sequence<sizeof...(Is)-1U-Is...>{} );

    template <std::size_t N>
    using make_index_reverse_sequence = decltype(indexSequenceReverse(std::make_index_sequence<N>{}));

    template<typename Func, typename TupleT, std::size_t TupleSize = std::tuple_size_v<std::decay_t<TupleT>>>
    void for_each(Func&& func, TupleT&& tuple) {
        for_each_helper(std::forward<Func>(func), std::forward<TupleT>(tuple),
                        std::make_index_sequence<TupleSize>());
    }

    template<typename Func, typename TupleT, std::size_t TupleSize = std::tuple_size_v<std::decay_t<TupleT>>>
    void rfor_each(Func&& func, TupleT&& tuple) {
        for_each_helper(std::forward<Func>(func), std::forward<TupleT>(tuple),
                        make_index_reverse_sequence<TupleSize>());
    }

    /// \brief locks in scope mutex1, mutex2, ... mutexN. unlocks mutexN..., mutex2, mutex1
    template<typename ...Mutexes>
    class hierarchy_scoped_lock {
    public:
        explicit hierarchy_scoped_lock(Mutexes&... mutexes): m_mutexes(std::tie(mutexes...))
        { for_each([](auto&& __m){ __m.lock(); }, m_mutexes); }

        hierarchy_scoped_lock(const hierarchy_scoped_lock& other) = delete;
        hierarchy_scoped_lock& operator=(const hierarchy_scoped_lock& other) = delete;

        ~hierarchy_scoped_lock() { rfor_each([](auto&& __m){__m.unlock();}, m_mutexes); }
    private:
        std::tuple<Mutexes&...> m_mutexes;
    };


    template<typename Mutex>
    thread_local ulong hierarchical_mutex<Mutex>::this_thread_priority(ULONG_MAX);

    /// \brief usefull for single producer / consumer( * n (1 < 4) )
    template<typename T, typename Allocator = std::allocator<T>>
    class blocking_threadsafe_queue {
    private:

        struct Node {
            Node() noexcept = default;
            explicit Node(T&& val): value(std::move(val)) {}
            Node* next { nullptr };
            T value {};
        };

        struct NodeIterator {
            using value_type = Node;


            NodeIterator() noexcept = default;
            explicit NodeIterator(Node* node) noexcept: m_node(node) {}

            Node& operator*() const noexcept{ return *m_node; }
            Node* operator -> () const noexcept { return m_node; }
            NodeIterator& operator++() noexcept{
                m_node = m_node -> next;
                return *this;
            }
            bool operator==(const NodeIterator& other) {
                return m_node == other.m_node;
            }
            bool operator!=(const NodeIterator& other) {
                return !((*this) == other);
            }
        private:
            Node* m_node { nullptr };
        };

        using t_alloc_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
        using t_alloc_traits = std::allocator_traits<t_alloc_type>;

        using node_alloc_type = typename t_alloc_traits::template rebind_alloc<Node>;
        using node_alloc_traits = std::allocator_traits<node_alloc_type>;

        using uninitialized_shifter = uninitialized_shift<std::is_move_constructible_v<Node>>;
        using hierarchy_mutex = hierarchical_mutex<rstd::spinlock>;

        Node m_dummyNode;
    public:
        blocking_threadsafe_queue(const blocking_threadsafe_queue& other) = delete;
        blocking_threadsafe_queue& operator=(const blocking_threadsafe_queue& other) = delete;
        blocking_threadsafe_queue(blocking_threadsafe_queue&& other) = delete;
        blocking_threadsafe_queue& operator=(blocking_threadsafe_queue&& other) = delete;

        blocking_threadsafe_queue(const Allocator& alloc = std::allocator<T>(),
                    std::size_t startCapacity = 10):
            m_head(nullptr), m_tail(nullptr), m_alloc(alloc), m_cap{startCapacity} {
            m_nodeBuffer = node_alloc_traits::allocate(m_alloc, m_cap);
           m_head = &m_dummyNode;
           m_tail = &m_dummyNode;
        }

        ~blocking_threadsafe_queue() {
            for(auto start = begin(); start != end(); ++start)
                node_alloc_traits::destroy(m_alloc, std::addressof(*start));

            node_alloc_traits::deallocate(m_alloc, m_nodeBuffer, sizeof(Node) * m_len);
        }

        /// \brief not thread-safe method, protect outside
        void clear() {
            for(auto start = NodeIterator(m_nodeBuffer); start != end(); ++start)
                node_alloc_traits::destroy(m_alloc, std::addressof(*start));

            m_len = 0;
            m_head = &m_dummyNode;
            m_tail = &m_dummyNode;
        }

        void push(T&& value) {
            m_tailMutex.lock();
            auto newNode = allocateNode();
            node_alloc_traits::construct(m_alloc, newNode, std::move(value));

            m_tail -> next = newNode;
            m_tail = newNode;
            ++m_len;
            m_tailMutex.unlock();
        }

        std::shared_ptr<T> try_pop() {
            std::lock_guard<hierarchy_mutex> lockGuard{m_headMutex};
            auto old_head = pop_head();
            if(!old_head)
                return nullptr;
            auto ret = std::make_shared<T>(old_head -> value);
            return ret;
        }
    private:
        //// TODO(a.raag): more nice way 1048576 2^20
        std::size_t max_size() const { return std::pow(2, 20); }

        NodeIterator begin() noexcept{ return NodeIterator { m_head }; }
        NodeIterator end() noexcept { return NodeIterator { m_tail -> next }; }

        Node* pop_head() {
            if(m_head == get_tail())
                return nullptr;
            auto old_head = std::move(m_head);
            m_head = std::move(old_head -> next);
            return (old_head == &m_dummyNode) ? nullptr: old_head;
        }

        Node* get_tail() {
            std::lock_guard<hierarchy_mutex> spinGuard{ m_tailMutex };
            return m_tail;
        }

        Node* allocateNode() {
            if(m_len < m_cap)
                return m_nodeBuffer + m_len;
            else {
                m_tailMutex.unlock();
                relacateBuffer();
                m_tailMutex.lock();
                return m_nodeBuffer + m_len;
            }
        }

        void relacateBuffer() {
            hierarchy_scoped_lock lock{m_headMutex, m_tailMutex};
            if(m_cap * 2 <= max_size())
                m_cap *= 2;
            else {
                /// ???? what todo on max size ??
            }
            Node* newBuffer = node_alloc_traits::allocate(m_alloc, m_cap);
            Node* newStart = newBuffer;
            Node* newFinish = newStart;
            auto len = m_len;

            try {
                Node* newStartIter = newBuffer;
                NodeIterator oldStart = begin();
                auto oldEnd = end();

                for(;oldStart != oldEnd; ++oldStart, ++newStartIter) {
                    Node* newNode = newStartIter;
                    node_alloc_traits::construct(m_alloc, newNode, std::move(*oldStart));
                    newFinish -> next = newNode;
                    newFinish = newNode;
                }
            }
            catch(...) {
                for(auto first = begin(); first != end(); ++first)
                    node_alloc_traits::destroy(m_alloc, std::addressof(*first));
                node_alloc_traits::deallocate(m_alloc, newBuffer, len);
            }

            for(auto first = NodeIterator(m_nodeBuffer); first != end(); ++first)
                node_alloc_traits::destroy(m_alloc, std::addressof(*first));
            node_alloc_traits::deallocate(m_alloc, m_nodeBuffer, len);

            m_head = newStart;
            m_tail = newFinish;
            m_nodeBuffer = newBuffer;
        }
    private:
        hierarchy_mutex m_headMutex{1500};
        hierarchy_mutex m_tailMutex{1000};

        Node* m_head;
        Node* m_tail;
        Node* m_nodeBuffer { nullptr };

        std::size_t m_len { 0 };
        std::size_t m_cap { 0 };
        node_alloc_type m_alloc;
    };

}



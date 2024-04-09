#include <collections/concurrency/thread_pool.hpp>
#include <collections/concurrency/future.hpp>

#include <iostream>
using namespace std::chrono_literals;

void single_subscribe(concurrency::iexecutor& executor) {
    auto first = []() {
        return 42;
    };
    auto f = concurrency::async(executor, first)
            .subscribe(executor, [](concurrency::Result<int>&& result) {

            });
    /// Idea that future can't be using somewhere else, because we work with subscribe / then but not get() method directly.
    MARK_UNUSABLE(f)
}

void many_then(concurrency::iexecutor& executor) {
    auto first = []() {
        return 40;
    };
    auto second = [](concurrency::Result<int>&& result) {
        return result.ValueOrThrow() + 1;
    };
    auto third = [](concurrency::Result<int>&& result) {
        result.ValueOrThrow() + 1;
    };
    auto f = concurrency::async(executor, first)
            .then(executor, second)
            .then(executor, third);

    MARK_UNUSABLE(f)
}

struct ThenRecoverException: public std::runtime_error {
    ThenRecoverException(std::string message): std::runtime_error(std::move(message)) {}
    const char* what() const noexcept override {
        return "ThenRecoverException";
    }
};

void then_recover(concurrency::iexecutor& executor) {
    auto first = []() {
        /// ... some code why we should throw exception
        throw ThenRecoverException("then_recover first throw");
        return 40;
    };
    /// handle possible exception
    auto second = [](concurrency::Result<int>&& result) {
        return 1;
    };
    auto third = [](concurrency::Result<int>&& result) {
        int val = result.ValueOrThrow() + 1;
    };
    auto f = concurrency::async(executor, first)
            .recover(executor, second)
            .then(executor, third);

    MARK_UNUSABLE(f)
}

void run() {
    concurrency::thread_pool pool{4};

    for(int i = 0; i < 100000; ++i) {
        single_subscribe(pool);
        many_then(pool);
        then_recover(pool);
    }

    pool.waitIdle();
}


struct MyClass {
    MyClass() = default;
    MyClass(int val): value(val) {}
    MyClass(const MyClass& other) = default;
    MyClass& operator=(const MyClass& other) = default;
    MyClass(MyClass&& other) = default;
    MyClass& operator=(MyClass&& other) = default;
    ~MyClass() = default;

    friend std::ostream& operator<<(std::ostream& os, const MyClass& copyOnly);

    int value { 0 };
};

std::ostream& operator<<(std::ostream& os, const MyClass& copyOnly) {
    os << copyOnly.value;
    return os;
}


template<typename T>
class MyQueueAllocator {
public:
    using value_type = T;

    MyQueueAllocator() = default;

    template<typename U>
    MyQueueAllocator(const MyQueueAllocator<U>& other) {}

    T* allocate(std::size_t n) {
        void* buffer = ::operator new(n * sizeof(T));
        AllocNode* allocNode;
        allocNode -> allocSize = n * sizeof(T);
        allocNode -> alignment = 0; // ?
        allocNode -> buffer = buffer;

        allocNode -> next = m_tail;
        allocNode -> prev = m_tail;
        m_tail = allocNode;

        return reinterpret_cast<T*>( buffer );
    }

    void deallocate(void* memPointer, std::size_t n) {
        ::operator delete(memPointer);
    }

    template<typename U, typename ... Args>
    void construct(T* memPointer, Args&& ... args) {
        new(memPointer) U(std::forward<Args>(args)...);
    }

    template<typename U>
    void destroy(U* memPointer) {
        if(memPointer)
            memPointer -> ~U();
    }

private:
    struct AllocNode {
        std::size_t allocSize { 0 };
        std::size_t alignment { 0 };
        void* buffer { nullptr };
        AllocNode* next { nullptr };
        AllocNode* prev { nullptr };
    };


    AllocNode* m_head;
    AllocNode* m_tail;
};

template<typename T, typename U>
constexpr bool operator==(const MyQueueAllocator<T>&, const MyQueueAllocator<U>& ) {
    return true;
}

template<typename T, typename U>
constexpr bool operator!=(const MyQueueAllocator<T>&, const MyQueueAllocator<U>& ) {
    return false;
}

template<typename T, typename Allocator>
class threadsafe_queue_base {
public:
};

template<typename T, typename Allocator = std::allocator<T>>
class threadsafe_queue: protected threadsafe_queue_base<T, Allocator> {
private:

    struct Node {
        Node() = default;
        explicit Node(T&& value): value(std::move(value)) {}
        ~Node() = default;

        Node* next { nullptr };
        T value;
    };


    using t_alloc_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
    using t_alloc_traits = std::allocator_traits<t_alloc_type>;

    using node_alloc_type = typename t_alloc_traits::template rebind_alloc<Node>;
    using node_alloc_traits = std::allocator_traits<node_alloc_type>;

public:
    threadsafe_queue(const Allocator& alloc = std::allocator<T>()):
            m_alloc(node_alloc_type(alloc)) {}
    threadsafe_queue(const threadsafe_queue& other) = delete;
    threadsafe_queue& operator=(const threadsafe_queue& other) = delete;
    threadsafe_queue(threadsafe_queue&& other) = delete;
    threadsafe_queue& operator=(threadsafe_queue&& other) = delete;
    ~threadsafe_queue() {
        clear();
    }

    void clear() {
        Node* curr = m_head;
        Node* prev = m_head;


        while (curr) {
            curr = curr -> next;
            node_alloc_traits::destroy(m_alloc, prev);
            node_alloc_traits::deallocate(m_alloc, prev, sizeof(Node));
            prev = curr;
        }
    }

    void pop_front() {
        Node* curr = m_head;
        curr = curr -> next;
        node_alloc_traits::destroy(m_alloc, m_head);
        node_alloc_traits::deallocate(m_alloc, m_head, sizeof(Node));
        m_head = curr;
    }

    void push(T&& value) {
        if(m_head == nullptr) {
            m_head = node_alloc_traits::allocate(m_alloc, 1);
            node_alloc_traits::construct(m_alloc, m_head, std::move(value));
            m_tail = m_head;
        }
        else {
            auto newNode = node_alloc_traits::allocate(m_alloc, 1);
            node_alloc_traits::construct(m_alloc, newNode, std::move(value));

            m_tail -> next = newNode;
            m_tail = newNode;
        }
        ++m_size;
    }

    void reverse() {
        Node* curr = m_head;
        Node* prev = nullptr;
        Node* next = nullptr;
        m_tail = m_head;

        while(curr) {
            next = curr -> next;
            curr -> next = prev;
            prev = curr;
            curr = next;
        }

        m_head = prev;
    }

    void print() {
        Node* currIter = m_head;

        while(currIter) {
            std::cout << currIter -> value << " -> ";
            currIter = currIter -> next;
        }

        std::cout << std::endl;
    }


    [[nodiscard]] bool empty() const noexcept {
        return m_head == nullptr;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return m_size;
    }

private:
    Node* m_head { nullptr };
    Node* m_tail { nullptr };
    std::size_t m_size { 0 };

    node_alloc_type m_alloc;
};


using QueueAllocator = MyQueueAllocator<MyClass>;
void queue_example(QueueAllocator& allocator) {


    threadsafe_queue<MyClass, QueueAllocator> t(allocator);
    t.push(MyClass{1});
    t.push(MyClass{2});
    t.push(MyClass{3});
    t.push(MyClass{4});
    t.print();
}


namespace collections::concurrency::GC {
    namespace details {

        using hazard_ptr_handle = void*;

        class hazard_pointer_guard {
        public:
            template <typename T>
            T* operator=( T* ptr ) noexcept
            {
                set( ptr );
                return ptr;
            }

            std::nullptr_t operator=( std::nullptr_t ) noexcept
            {
                clear();
                return nullptr;
            }

            template<typename T>
            void set(T* ptr) noexcept {
                m_hp.store(reinterpret_cast<hazard_ptr_handle>(ptr), std::memory_order::memory_order_release);
            }

            void clear() noexcept
            {
                clear(std::memory_order_release );
            }

            void clear(std::memory_order order ) noexcept
            {
                m_hp.store( nullptr, order );
            }
        public:
            hazard_pointer_guard* next { nullptr };
        private:
            std::atomic<hazard_ptr_handle> m_hp { nullptr };
        };


        /// \brief Hazard pointer storage per thread
        struct thread_hp_storage {};

        struct thread_data {
            thread_hp_storage hazards;


            /// TODO(a.raag): padding or alignas single or double ??
            alignas(64) std::atomic<unsigned int> a_sync;


            thread_data() = delete;
            thread_data(thread_data const &) = delete;
            thread_data(thread_data&& ) = delete;

            void sync() {
                a_sync.fetch_add(1, std::memory_order::memory_order_acq_rel);
            }
        };

        class TLSManager {
        public:
            static thread_data* getTLS() {
                return m_tlsData;
            }

            static void setTLS(thread_data* td) {
                m_tlsData = td;
            }

        private:
            static thread_local thread_data* m_tlsData;
        };


        class basic_smr {
        private:
            template<typename TLSManager>
            friend class generic_smr;

            struct thread_record: thread_data
            {
                // next hazard ptr record in list
                thread_record*                      next_ = nullptr;
                // Owner thread_record; nullptr - the record is free (not owned)
                std::atomic<thread_record*>  owner_rec_;
                // true if record is free (not owned)
                std::atomic<bool>               free_{ false };

//                thread_record( guard* guards, size_t guard_count, retired_ptr* retired_arr, size_t retired_capacity )
//                        : thread_data( guards, guard_count, retired_arr, retired_capacity ), owner_rec_(this)
//                {}
            };

        public:
            static basic_smr& getInstance() {
                return *m_instance;
            }

        private:
            static basic_smr* m_instance;
        };

        /// \brief Safe memory reclamation wrapper
        template<typename TLSManager>
        class SMR: public basic_smr {
        public:
            static thread_data* getTls() {
                thread_data* data = TLSManager::getTLS();
                return data;
            }
            /// Attach current thread to HP
            static void attach_thread()
            {
                // if ( !TLSManager::getTLS() )
                   // TLSManager::setTLS(getInstance().alloc_thread_data());
            }

            /// Detach current thread from HP
            static void detach_thread()
            {
                thread_data* rec = TLSManager::getTLS();
                if ( rec ) {
                    TLSManager::setTLS(nullptr);
                    // getInstance().free_thread_data(static_cast<thread_record*>( rec ), true );
                }
            }
        };
    }

    template<typename TLSManager>
    class hazard_pointer {
    private:
        using hp_implementation = details::SMR<TLSManager>;

    public:

        /// \brief Hazard Pointer allocation
        class Guard {
        public:
            Guard() {

            }

            explicit Guard( std::nullptr_t ) noexcept: m_hpGuard( nullptr )
            {}


            Guard(const Guard& other) = delete;
            Guard& operator=(const Guard& other) = delete;

            Guard(Guard&& other) noexcept: m_hpGuard(other.m_hpGuard) {
                other.m_hpGuard = nullptr;
            }

            Guard& operator=(Guard&& other) noexcept {
                std::swap(m_hpGuard, other.m_hpGuard);
                return *this;
            }

            Guard& operator=(std::nullptr_t) {
                return *this;
            }

            ~Guard() {
                unlink();
            }

            void unlink() {
                if(m_hpGuard) {
                    /// free
                    m_hpGuard = nullptr;
                }
            }

            template<typename T>
            T protect(const std::atomic<T>& nonGuarded) {
                T current = nonGuarded.load(std::memory_order::memory_order_relaxed);
                T ret;
                do {
                    ret = current;
                    assign(current);
                    current = nonGuarded.load(std::memory_order::memory_order_acquire);
                } while (current != ret);
                return current;
            }

            template<typename T>
            T* assign(T* p) {
                // assert(m_hpGuard);
                m_hpGuard -> set(p);
                hp_implementation::getTls() -> sync();
                return p;
            }

        private:
            details::hazard_pointer_guard* m_hpGuard;
        };

    public:
    };

    using defaultHazardPointer = hazard_pointer<details::TLSManager>;

}




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
        ~ms_queue() = default;


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

        bool try_pop(T& value) {
            if(do_try_pop(value))
                return true;
            return false;
        }

        [[nodiscard]] bool empty() const noexcept {
            /// TODO(a.raag): check m_head -> next != nullptr due to we have always dummy node
        }

        [[nodiscard]] std::size_t size() const noexcept {
            return m_itemCntr.load(std::memory_order::memory_order_seq_cst);
        }
    private:
        bool do_try_pop(T& value) {
            while(true) {

            }
            m_itemCntr.fetch_sub(1, std::memory_order::memory_order_seq_cst); // ? not so strong mem order
            return false;
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
                /// detach thread
            }
        };

    public:
        static ThreadGarbageGuardian attach_thread();

    private:
        GC::defaultHazardPointer m_garbageCollector;
    };


    GarbageCollector::ThreadGarbageGuardian GarbageCollector::attach_thread() {
        return {};
    }




}

template<typename T>
using lock_free_queue = collections::concurrency::ms_queue<T>;
using int_lock_free_queue = lock_free_queue<int>;
constexpr int perThreadWriteOps = 10000;

void lock_free_writer(int_lock_free_queue& queue) {
    auto&& guardian = collections::concurrency::GarbageCollector::attach_thread();
    for(int i = 0; i < perThreadWriteOps; ++i) {
        queue.push(std::move(i));
    }


}

void lock_free_reader(int_lock_free_queue& queue) {
    auto&& guardian = collections::concurrency::GarbageCollector::attach_thread();
}


void run_lock_free_example() {
    int_lock_free_queue queue;
    std::thread writer1{lock_free_writer, std::ref(queue)};
    std::thread writer2{lock_free_writer, std::ref(queue)};
    std::thread writer3{lock_free_writer, std::ref(queue)};

    std::thread reader1{lock_free_reader, std::ref(queue)};
    std::thread reader2{lock_free_reader, std::ref(queue)};
    std::thread reader3{lock_free_reader, std::ref(queue)};

    writer1.join();
    writer2.join();
    writer3.join();

    reader1.join();
    reader2.join();
    reader3.join();
}





int main () {
    collections::concurrency::GarbageCollector garbageCollector;
    run_lock_free_example();
    return 0;
}
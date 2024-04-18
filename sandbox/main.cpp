#include <collections/concurrency/thread_pool.hpp>
#include <collections/concurrency/future.hpp>
#include <collections/queue/lockfree_queue.hpp>
#include <collections/util/fmt.hpp>

#include "benchmark.hpp"

#include <iostream>
#include <cassert>
#include <memory.h> // memset
#include <climits>
#include <complex>

using namespace std::chrono_literals;

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




namespace robot2D {

#define TASK_PTR(Class) using Ptr = std::shared_ptr<Class>;
    class Task {
    public:
        using Ptr = std::shared_ptr<Task>;
        virtual ~Task() = 0;
        virtual void asyncExecute() = 0;
    };

    Task::~Task() = default;

    class Image {
    public:
        Image() = default;
        bool load() {
            m_vec.push_back(42);
            m_vec.push_back(89);
            m_vec.push_back(26);
            return true;
        }


        std::vector<int> m_vec;
    };

    class LoadImageTask: public Task {
    public:
        TASK_PTR(LoadImageTask)

        LoadImageTask(const std::string& s): m_path(s) {}
        void asyncExecute() override {
            std::this_thread::sleep_for(10ms);
            // throw std::runtime_error("LoadImageTaskException");
            m_image.load();
        }

        Image&& getImage() { return std::move(m_image); }
        const std::string& getPath() const { return m_path; }
    private:
        Image m_image;
        std::string m_path;
    };

    template<typename T>
    class TaskDispatcherAllocator {
    public:
        using value_type = T;
    };

    template<typename T>
    class MainCallable {
    private:
        using Result = collections::concurrency::Result<T>;
    public:
        explicit MainCallable(collections::concurrency::future<T>&& f): m_future(std::move(f)) {}
        MainCallable() = delete;
        MainCallable(const MainCallable& other) = delete;
        MainCallable& operator=(const MainCallable& other) = delete;
        MainCallable(MainCallable&& other) = default;
        MainCallable& operator=(MainCallable&& other) = default;

        template<typename Func>
        MainCallable<T>&& performMain(Func&& func)  {
            auto recoverCallback = [func = std::forward<Func>(func)](Result&& result) mutable {
                try {
                    auto value = result.ValueOrThrow();
                    func(value);
                }
                catch(const std::exception& exception) {

                }
            };
            std::move(m_future).subscribe(recoverCallback);
            return std::move(*this);
        }

        template<typename Func>
        void onError(Func&& func) && {
            m_failureCallback = std::forward<Func>(func);
        }
    private:
        collections::concurrency::future<T> m_future;
        collections::rstd::unique_function<void(const std::exception&)> m_failureCallback{};
    };

    template<typename T, typename U>
    struct FutureUnpacker {
        static U unpack(T&&);
    };

    template<typename T>
    class TaskFuture {
    private:
        using Result = collections::concurrency::Result<T>;
    public:
        TaskFuture(collections::concurrency::iexecutor* iexecutor,
                            collections::concurrency::future<T>&& f): m_future(std::move(f)), m_mainExecutor{iexecutor} {}

        template<typename Func>
        auto then(Func&& func) && -> TaskFuture<decltype(func(std::declval<T>()))> {
            using U = decltype(func(std::declval<T>()));

            /// continue function : std::shared_ptr<Task> -> U
            auto unpackCallback = [func = std::forward<Func>(func)](Result&& result) {
                auto&& res = result.ValueOrThrow();
                return func(std::move(res));
            };

            auto f = std::move(m_future).then(std::move(unpackCallback));
            TaskFuture<U> thenFuture(m_mainExecutor, std::move(f));
            return thenFuture;
        }

        template<typename Func>
        MainCallable<T> performMain(Func&& func) &&  {
            auto unpackCallback = [func = std::forward<Func>(func)](Result&& result) {
                auto&& res = result.ValueOrThrow();
                return func(std::move(res));
            };
            m_future.via(m_mainExecutor)
                .subscribe(std::move(unpackCallback));
            return MainCallable(std::move(m_future));
        }

    private:
        collections::concurrency::future<T> m_future;
        collections::concurrency::iexecutor* m_mainExecutor{ nullptr };
    };


    enum class TaskPriority: std::int32_t {
        UI = 0,
        Default = 1,
        Background = 2
    };

    class TaskDispatcher: public collections::concurrency::iexecutor {
    private:
        template<typename T>
        class TaskHolder {
        private:
            using HolderPtr = std::weak_ptr<T>;
            using RetPtr = std::shared_ptr<T>;
        public:
            TaskHolder() = delete;
            explicit TaskHolder(RetPtr&& p): m_ptr{ std::move(p) }{}
            TaskHolder(const TaskHolder& other) = default;
            TaskHolder& operator=(const TaskHolder& other) = default;
            TaskHolder(TaskHolder&& other) = default;
            TaskHolder& operator=(TaskHolder&& other) = default;
            ~TaskHolder() = default;

            RetPtr operator()() {
                if(!m_ptr)
                    return nullptr;
                m_ptr -> asyncExecute();
                return m_ptr;
            }
        private:
            RetPtr m_ptr { nullptr };
        };
    public:
        TaskDispatcher(const TaskDispatcher& other) = delete;
        TaskDispatcher& operator=(const TaskDispatcher& other) = delete;
        TaskDispatcher(TaskDispatcher&& other) = delete;
        TaskDispatcher& operator=(TaskDispatcher&& other) = delete;
        ~TaskDispatcher() override {
            m_pool.join();
        }

        static TaskDispatcher* getDispatcher() {
            static TaskDispatcher dispatcher;
            return &dispatcher;
        }

        int callCount = 0;
        void pollTasks();
    private:
        TaskDispatcher() = default;

        template<typename T, typename ... Args>
        auto asyncDispatch(Args&& ... args) {

            auto task = std::make_shared<T>(std::forward<Args>(args)...);

            TaskHolder<T> holder{std::move(task)};
            auto f = collections::concurrency::async(m_pool, std::move(holder))
                    .recover(m_pool, [](auto&& error) {
                        error.throwIfError();
                        return std::shared_ptr<T>(nullptr);
                    })
                    .via(this);
            return std::move(f);
        }

        template<typename T, typename ... Args>
        auto asyncDispatchChain(Args&& ... args) {

            auto task = std::make_shared<T>(std::forward<Args>(args)...);

            TaskHolder<T> holder{std::move(task)};
            auto f = collections::concurrency::async(m_pool, std::move(holder))
                    .recover(m_pool, [](auto&& error) {
                        error.throwIfError();
                        return std::shared_ptr<T>(nullptr);
                    });
            return std::move(f);
        }

        void execute(collections::concurrency::Task task) override {
            m_blockqueue.push(std::move(task));
        }


    private:
        using ExecuteTask = collections::concurrency::Task;
        friend class TaskDispatcherWrapper;

        collections::concurrency::thread_pool m_pool;
        //collections::concurrency::blocking_threadsafe_queue<ExecuteTask, TaskDispatcherAllocator> m_blockqueue;
        collections::concurrency::dummy_threadsafe_queue<ExecuteTask> m_blockqueue;
    };


    void TaskDispatcher::pollTasks() {
        ExecuteTask task;
        if(m_blockqueue.try_pop(task)) {
            if(task) {
                ++callCount;
                task();
            }

        }
    }

    namespace task_typetraits {
        template<typename ...> using VoidT = void;
        template<typename, typename = VoidT<>>
        struct HasPtrType: std::false_type {};

        template<typename T>
        struct HasPtrType<T, VoidT<typename T::Ptr>>: std::true_type {};

        template<typename T>
        inline constexpr bool has_ptr_type_v = HasPtrType<T>::value;
    }


    struct TaskDispatcherWrapper {
        template<typename T, typename = std::enable_if_t<task_typetraits::has_ptr_type_v<T>>, typename ... Args>
        static auto asyncTask(Args&& ... args) {
            auto dispatcher = TaskDispatcher::getDispatcher();
            static_assert(task_typetraits::has_ptr_type_v<T>, "MUST HAVE PTR TYPENAME");

            auto f = dispatcher -> asyncDispatch<T>(std::forward<Args>(args)...);
            MainCallable<typename T::Ptr> callable(std::move(f));
            return std::move(callable);
        }


        template<typename T, typename = std::enable_if_t<task_typetraits::has_ptr_type_v<T>>, typename ... Args>
        static auto asyncTaskChain(Args&& ... args) {
            auto dispatcher = TaskDispatcher::getDispatcher();
            static_assert(task_typetraits::has_ptr_type_v<T>, "MUST HAVE PTR TYPENAME");

            auto f = dispatcher -> asyncDispatchChain<T>(std::forward<Args>(args)...);
            TaskFuture<typename T::Ptr> callable(dispatcher, std::move(f));
            return std::move(callable);
        }
    };

    template<typename T, typename = std::enable_if_t<task_typetraits::has_ptr_type_v<T>>, typename ... Args>
    static auto asyncTask(Args&& ... args) {
        return TaskDispatcherWrapper::asyncTask<T>(std::forward<Args>(args)...);
    }

    template<typename T, typename = std::enable_if_t<task_typetraits::has_ptr_type_v<T>>, typename ... Args>
    static auto asyncTaskChain(Args&& ... args) {
        return TaskDispatcherWrapper::asyncTaskChain<T>(std::forward<Args>(args)...);
    }
}




class MainThreadExecutor: public  collections::concurrency::iexecutor {
public:
    ~MainThreadExecutor() override = default;

    void execute(collections::concurrency::Task task) override {
        m_taskQueue.push(std::move(task));
    }

    void process() {
        while(!m_taskQueue.empty()) {
            collections::concurrency::Task t;
            if(m_taskQueue.try_pop(t)) {
                t();
            }
        }
    }

    int getCallCount() { return m_taskQueue.size(); }
private:
    int m_calls { 0 };
    collections::concurrency::dummy_threadsafe_queue<collections::concurrency::Task> m_taskQueue;

};



void futures_benchmark() {

    collections::concurrency::thread_pool m_pool{ 4 };
    MainThreadExecutor mainThreadExecutor;

    constexpr int asyncOpsValue = 100000;

    for(int i = 0; i < asyncOpsValue; ++i) {
        auto func1 = []() {
            return 40;
        };

        auto func2 = [](collections::concurrency::Result<int>&& result) {
            auto val = result.ValueOrThrow();
            return val + 1;
        };
        auto func3 = [](collections::concurrency::Result<int>&& result) {
            auto val = result.ValueOrThrow();
            return val + 1;
        };
        auto func4 = [](collections::concurrency::Result<int>&& result) {
            assert(result.ValueOrThrow() == 42);
        };

        collections::concurrency::async(m_pool, std::move(func1))
                .then(m_pool, std::move(func2))
                .then(m_pool, std::move(func3))
                .via(&mainThreadExecutor)
                .subscribe(func4);
    }

    m_pool.waitIdle();
    m_pool.join();
}



namespace robot2D {
    template<>
    struct FutureUnpacker<LoadImageTask, Image> {
        static Image unpack(LoadImageTask&& task) {
            return {};
        }
    };
}

std::vector<int> assertVec = { 42, 89, 26, 14 };
std::string imagePath = "res/img/icon.png";

void stressDispatcher() {

    for(int i = 0; i < 1000; ++i) {

        auto filterFunction = [](robot2D::LoadImageTask::Ptr loadImageTask) {
            return std::move(loadImageTask -> getImage());
        };

        auto tmpFunction = [](robot2D::Image&& image) {
            image.m_vec.push_back(14);
            return std::move(image);
        };

        robot2D::asyncTaskChain<robot2D::LoadImageTask>(imagePath)
                .then(std::move(filterFunction))
                .then(std::move(tmpFunction))
                .performMain([](robot2D::Image&& image) {
                    assert(assertVec == image.m_vec);
                });

    }

}


namespace {
    collections::concurrency::dummy_threadsafe_queue<int> g_dummyQueue;
    collections::concurrency::blocking_threadsafe_queue<int> g_Queue;


    constexpr int opsValue = 10000; // 200 400 800 1600
    constexpr int threadPairValue = 2;
    std::atomic_int g_popDummy{ 0 };
    std::atomic_int g_DummyCntr{ 0 };
    std::atomic_int g_popQueue{ 0 };
    std::atomic_int g_QueueCntr{ 0 };

}

void dummy_writer() {
    for(int i = 0; i < opsValue; ++i) {
        int a = i;
        g_dummyQueue.push(std::move(a));
    }

}


void dummy_reader() {
    while(g_popDummy.load() < opsValue) {
        if(auto task = g_dummyQueue.try_pop()) {
            g_popDummy.fetch_add(1);
        }
    }
}


void dummy_benchmark(int threadPairVal) {
    std::vector<std::thread> writerThreads;
    std::vector<std::thread> readerThreads;
    for(int i = 0; i < threadPairVal; ++i)
        writerThreads.emplace_back(dummy_writer);
    for(int i = 0; i < threadPairVal * 2; ++i)
        readerThreads.emplace_back(dummy_reader);

    for(auto& writer: writerThreads)
        writer.join();

    for(auto& reader: readerThreads)
        reader.join();

    g_popDummy = 0;
    g_dummyQueue.clear();
}

void queue_writer() {
    for(int i = 0; i < opsValue + 1; ++i) {
        int a = i;
        g_Queue.push(std::move(a));
    }
}

void queue_reader() {
    while(g_popQueue.load() < opsValue) {
        if(auto val = g_Queue.try_pop()) {
            g_popQueue.fetch_add(1);
        }
    }
}

void queue_benchmark(int threadPairVal) {
    std::vector<std::thread> writerThreads;
    std::vector<std::thread> readerThreads;
    for(int i = 0; i < threadPairVal; ++i)
        writerThreads.emplace_back(queue_writer);
    for(int i = 0; i < threadPairVal * 2; ++i)
        readerThreads.emplace_back(queue_reader);

    for(auto& writer: writerThreads)
        writer.join();

    for(auto& reader: readerThreads)
        reader.join();

    g_popQueue = 0;
    g_Queue.clear();
}

void dispatcherTest() {
    stressDispatcher();

    auto dispatcher = robot2D::TaskDispatcher::getDispatcher();
    while(dispatcher -> callCount != 1000) {
        robot2D::TaskDispatcher::getDispatcher() -> pollTasks();
    }

}


void queue_benchmarks() {
    const int try_count = 10;
    std::array<int, 2> threadCount = { 1, 2 };
    //std::array<int, 1> threadCount = { 1 };

    for(auto& i: threadCount) {
        util::BenchmarkManager benchmarkManager;

        util::Benchmark dummyBenchmark{"DummyBlockQueue", dummy_benchmark};
        util::Benchmark queueBenchmark{"FinedBlockQueue", queue_benchmark};
        auto t2 =
                benchmarkManager.do_benchmark(std::move(queueBenchmark), try_count, i);
        auto t1
                = benchmarkManager.do_benchmark(std::move(dummyBenchmark), try_count, i);

        std::cout << "ThreadCount Result: " << i << std::endl;
        auto min = std::min(t2, t1);
        auto max = std::max(t2, t1);
        std::cout <<
            collections::util::format_string("min time: {0} for: {1}", min.time.asMilliseconds(), min.name) << std::endl;
        std::cout <<
            collections::util::format_string("max time: {0} for: {1}", max.time.asMilliseconds(), max.name) << std::endl;
    }


}

void queue_bench() {
    util::BenchmarkManager benchmarkManager;

//    util::Benchmark dummyBenchmark{"DummyBlockQueue", dummy_benchmark};
//    util::Benchmark queueBenchmark{"FinedBlockQueue", queue_benchmark};
//    benchmarkManager.do_benchmark(std::move(dummyBenchmark), 5);
//    benchmarkManager.do_benchmark(std::move(queueBenchmark), 5);
//
//
//
//
//    for(auto res: benchmarkManager.getResults()) {
//        std::cout << res.time.asMilliseconds() << std::endl;
//    }

}

struct Node {
    int val;

    friend std::ostream& operator<<(std::ostream& os, const Node& node) {
        os << node.val;
        return os;
    }
};

struct NodeIterator {
    using value_type = Node;
    using iterator_category = std::forward_iterator_tag;
    /// ops ///

    NodeIterator() noexcept = default;
    explicit NodeIterator(Node* node) noexcept: m_node(node) {}

    Node& operator*() const noexcept{ return *m_node; }
    Node* operator -> () const noexcept { return m_node; }
    NodeIterator& operator++() noexcept{
        m_node++;
        return *this;
    }
    bool operator==(const NodeIterator& other) {
        return m_node == other.m_node;
    }
    bool operator!=(const NodeIterator& other) {
        return !((*this) == other);
    }
private:
    Node* m_node;
};



template<bool CanMove>
struct uninitialized_shift{};

template<>
struct uninitialized_shift<false> {
    template<typename InputIt, typename NoThrowForwardIt, typename Allocator>
    static NoThrowForwardIt shift(InputIt begin, InputIt end,
                                  NoThrowForwardIt d_first, Allocator& allocator) {
        using T = typename NoThrowForwardIt::value_type;
        using alloc_traits = std::allocator_traits<Allocator>;

        NoThrowForwardIt current = d_first;
        try {
            for(; begin != end; ++begin, (void)++current)
                alloc_traits::construct(allocator, std::addressof(*current), *begin);

            return current;
        }
        catch(...) {
            for (; d_first != current; ++d_first)
                alloc_traits::destroy(allocator, std::addressof(*d_first));
            throw;
        }
    }
};

template<>
struct uninitialized_shift<true> {
    template<typename InputIt, typename NoThrowForwardIt, typename Allocator>
    static NoThrowForwardIt shift(InputIt begin, InputIt end,
                                  NoThrowForwardIt d_first, Allocator& allocator) {
        using T = typename NoThrowForwardIt::value_type;
        using alloc_traits = std::allocator_traits<Allocator>;

        NoThrowForwardIt current = d_first;
        try {
            for(; begin != end; ++begin, (void)++current)
                alloc_traits::construct(allocator, std::addressof(*current), std::move(*begin));

            return current;
        }
        catch(...) {
            for (; d_first != current; ++d_first)
                alloc_traits::destroy(allocator, std::addressof(*d_first));
            throw;
        }
    }
};



template<typename T>
class queue_intrusive {
public:
    void push() {

    }

private:

};

template<typename T, typename Allocator = std::allocator<T>>
class queue: private queue_intrusive<T> {
private:

    struct Node {
        Node() = default;
        explicit Node(T&& value): value(std::move(value)) {}
        ~Node() = default;

        Node* next { nullptr };
        T value;
    };

    struct NodeIterator {
        using value_type = Node;
        /// ops ///

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
        Node* m_node;
    };

    using t_alloc_type = typename std::allocator_traits<Allocator>::template rebind_alloc<T>;
    using t_alloc_traits = std::allocator_traits<t_alloc_type>;

    using node_alloc_type = typename t_alloc_traits::template rebind_alloc<Node>;
    using node_alloc_traits = std::allocator_traits<node_alloc_type>;
    using uninitialized_shifter = uninitialized_shift<std::is_move_constructible_v<Node>>;
public:
    queue(const Allocator& alloc = std::allocator<T>()):
            m_alloc(node_alloc_type(alloc)) {
        m_nodeBuffer = node_alloc_traits::allocate(m_alloc, m_cap);
    }
    queue(const queue& other) = delete;
    queue& operator=(const queue& other) = delete;
    queue(queue&& other) = delete;
    queue& operator=(queue&& other) = delete;
    ~queue() {
        clear();
    }

    void clear() {
        for(auto start = begin(); start != end(); ++start)
            node_alloc_traits::destroy(m_alloc, std::addressof(*start));

        node_alloc_traits::deallocate(m_alloc, m_nodeBuffer, sizeof(Node) * m_size);
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
            m_head = allocateNode();
            node_alloc_traits::construct(m_alloc, m_head, std::move(value));
            m_tail = m_head;
        }
        else {
            auto newNode = allocateNode();
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
    NodeIterator begin() noexcept { return NodeIterator{ m_head }; }
    NodeIterator end() noexcept { return NodeIterator{ m_tail -> next }; }

    Node* allocateNode() {
        if(m_size < m_cap)
            return m_nodeBuffer + m_size;
        else {
            m_cap *= 2;
            Node* newBuffer = node_alloc_traits::allocate(m_alloc, m_cap);
            Node* newStart = newBuffer;
            Node* newFinish = newStart;
            auto len = m_size;

            try {
                Node* newStartIter = newBuffer;

                auto oldStart = begin();
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

            for(auto first = begin(); first != end(); ++first)
                node_alloc_traits::destroy(m_alloc, std::addressof(*first));
            node_alloc_traits::deallocate(m_alloc, m_nodeBuffer, len);

            m_head = newStart;
            m_tail = newFinish;
            m_nodeBuffer = newBuffer;
            return m_nodeBuffer + m_size;
        }
    }



private:
    Node* m_head { nullptr };
    Node* m_tail { nullptr };
    std::size_t m_size { 0 };

    std::size_t m_cap{ 2 };
    Node* m_nodeBuffer{ nullptr };
    node_alloc_type m_alloc;
};


using QueueAllocator = MyQueueAllocator<MyClass>;
void queue_example(QueueAllocator& allocator) {
    queue<MyClass, QueueAllocator> q(allocator);
    q.push(MyClass{1});
    q.push(MyClass{2});
    q.push(MyClass{3});
    q.push(MyClass{4});
    q.push(MyClass{5});
    q.push(MyClass{6});
    q.push(MyClass{7});
    q.push(MyClass{8});
    q.push(MyClass{9});
    q.print();
}

enum class OverMode {
    Default,
    Reverse
};

template<int index, OverMode = OverMode::Default>
struct iterate_tuple {
    template<typename Func, typename ...Ts>
    static void over(Func&& func, std::tuple<Ts...>& t) {
        OverMode mode;
        if(mode == OverMode::Default) {
            iterate_tuple<index-1>::over(std::forward<Func>(func), t);
            func(std::get<index>(t));
        }
        else {
            func(std::get<index>(t));
            iterate_tuple<index-1>::over(std::forward<Func>(func), t);
        }
    }
};

template<>
struct iterate_tuple<0> {
    template<typename Func, typename ...Ts>
    static void over(Func&& func, std::tuple<Ts...>& t) {
        func(std::get<0>(t));
    }
};

template<typename Func, typename TupleT, std::size_t... Is>
void for_each_helper(Func&& func, TupleT&& tuple, std::index_sequence<Is...>) {
    (func(std::get<Is>(std::forward<TupleT>(tuple))), ...);
}

template<typename Func, typename TupleT, std::size_t TupleSize = std::tuple_size_v<std::decay_t<TupleT>>>
void for_each(Func&& func, TupleT&& tuple) {
    for_each_helper(std::forward<Func>(func), std::forward<TupleT>(tuple),
                    std::make_index_sequence<TupleSize>());
}


template <std::size_t, typename>
struct make_reverse_index_sequence_helper;

template <std::size_t N, std::size_t...NN>
struct make_reverse_index_sequence_helper<N, std::index_sequence<NN...>>
        : std::index_sequence<(N - NN)...> {};

template <size_t N>
struct make_reverse_index_sequence
        : make_reverse_index_sequence_helper<N - 1,
                decltype(std::make_index_sequence<N>{})> {};


template <std::size_t ... Is>
constexpr auto indexSequenceReverse (std::index_sequence<Is...> const &)
-> decltype( std::index_sequence<sizeof...(Is)-1U-Is...>{} );

template <std::size_t N>
using make_index_reverse_sequence = decltype(indexSequenceReverse(std::make_index_sequence<N>{}));

template<typename Func, typename TupleT, std::size_t TupleSize = std::tuple_size_v<std::decay_t<TupleT>>>
void rfor_each(Func&& func, TupleT&& tuple) {
    for_each_helper(std::forward<Func>(func), std::forward<TupleT>(tuple),
                    make_index_reverse_sequence<TupleSize>());
}

int main () {
//    queue_benchmarks();
    MyQueueAllocator<MyClass> allocator{};
    queue_example(allocator);
    return 0;
}
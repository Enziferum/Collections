#include <collections/concurrency/thread_pool.hpp>
#include <collections/concurrency/future.hpp>
#include <collections/queue/lockfree_queue.hpp>
#include <collections/util/fmt.hpp>

#include "benchmark.hpp"

#include <iostream>
#include <cassert>

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


namespace collections::concurrency {

    template<typename T>
    class intrusive_blocking_threadsafe_queue {

    };

    template<typename T, typename Allocator>
    class blocking_threadsafe_queue {
    public:

        void push(T&& value);
        bool try_pop(T& value);
    private:

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

void lockfree()
{
    collections::concurrency::GarbageCollector garbageCollector;
    run_lock_free_example();
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


void benchmark1() {
    util::BenchmarkManager benchmarkManager;
    benchmarkManager.do_benchmark("AsyncFutures+rstd::spinlock", futures_benchmark);


    for(auto& result: benchmarkManager.getResults())
        std::cout << collections::util::format_string("Result is {0} ms", result.time.asMilliseconds()) << std::endl;
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

int main () {
    stressDispatcher();

    auto dispatcher = robot2D::TaskDispatcher::getDispatcher();
    while(dispatcher -> callCount != 1000) {
        robot2D::TaskDispatcher::getDispatcher() -> pollTasks();
    }

    return 0;
}
#pragma once

#include <exception>
#include <mutex>
#include <atomic>
#include <memory>
#include <future>
#include <functional>
#include <list>
#include <condition_variable>
#include <system_error>
#include <type_traits>

#include "collections/rstl/unique_function.hpp"
#include "iexecutor.hpp"
#include "result.hpp"

namespace concurrency {

    enum class future_error_type {

    };

    class future_error: public std::logic_error {
    public:
        explicit future_error(future_error_type type);
    private:
        explicit future_error(std::error_code code): std::logic_error("rstl::future " + code.message()) {}
    };

    template<typename R>
    class shared_state {
    public:
        shared_state() = default;
        shared_state(const shared_state& other) = delete;
        shared_state& operator=(const shared_state& other) = delete;
        shared_state(shared_state&& other) noexcept = delete;
        shared_state& operator=(shared_state&& other) noexcept = delete;
        ~shared_state() = default;
    private:
        template<typename U>
        friend class shared_future;

        template<typename U>
        friend class future;

        template<typename U>
        friend class promise;

        /// Result(R+exception) ///
        R value;
        std::exception_ptr exceptionPtr;

        std::mutex mutex;
        std::atomic_bool ready;
        std::condition_variable m_cond;
        std::list<Task> m_continuations;
        std::shared_ptr<Task> m_subscription;
    };

    template<>
    class shared_state<void> {
    public:
        shared_state() = default;
        shared_state(const shared_state& other) = delete;
        shared_state& operator=(const shared_state& other) = delete;
        shared_state(shared_state&& other) = delete;
        shared_state& operator=(shared_state&& other) = delete;
        ~shared_state() = default;
    private:
        template<typename U>
        friend class future;

        template<typename U>
        friend class promise;

        std::exception_ptr exceptionPtr;
        std::mutex mutex;
        std::atomic_bool ready;
        std::condition_variable m_cond;
        std::list<Task> m_continuations;
    };

    template<typename Signature>
    class packaged_task{};

    template<class R>
    struct shared_future;

    template<typename R>
    class future: private shared_future<R> {
    public:
        future() noexcept = default;
        future(const future& other) = delete;
        future& operator=(const future& other) = delete;
        future(future&& other) = default;
        future& operator=(future&& other) noexcept = default;
        ~future() noexcept = default;

        using shared_future<R>::valid;
        using shared_future<R>::ready;
        using shared_future<R>::wait;
        using shared_future<R>::attach_cancellable_task_state;


        void swap(future& other) noexcept {
            std::swap(this->m_state, other.m_state);
            std::swap(this->m_cancellable_task_state, other.m_cancellable_task_state);
        }


        shared_future<R> share() const {
            if (this -> m_state == nullptr) throw "no_state";
            return shared_future<R>(std::move(this -> m_state));
        }

        Result<R> get()  {
           wait();
           auto state = std::move(this->m_state);
           if(state -> exceptionPtr)
               return Result<R>::Fail({state -> exceptionPtr});
           return Result<R>::Ok(std::move(state -> value));
        }

        template<typename Func>
        auto subscribe(iexecutor& executor, Func&& func) -> future<decltype(func(std::declval<Result<R>>()))> {
            if(!this -> m_state)
                throw std::runtime_error("concurrency::future: state is nullptr");
            auto state = this -> m_state;
            using R2 = decltype(func(std::declval<Result<R>>()));
            packaged_task<R2()> task(executor, [func = std::forward<Func>(func), fut = std::move(*this)]() mutable {
                return func(std::move(fut.nowait_get()));
            });

            future<R2> f = task.get_future();
            {
                std::lock_guard<std::mutex> lockGuard{state -> mutex};
                if(state -> ready) {
                    executor.execute(std::move(task));
                }
                else {
                    state -> m_subscription = std::make_shared<Task>(std::move(task));
                }
            }

            return f;
        }

        template<typename Func>
        auto then(iexecutor& executor, Func&& func)  {
            if(!this -> m_state)
                throw std::runtime_error("concurrency::future: state is nullptr");
            auto state = this -> m_state;

            using R2 = decltype(func(std::declval<Result<R>>()));
            packaged_task<R2()> task(executor,
                                     [func = std::forward<Func>(func), fut = std::move(*this)]() mutable {
                return func(fut.nowait_get());
            });

            future<R2> f = task.get_future();

           // {
                std::lock_guard<std::mutex> lockGuard(state -> mutex);
                if (state -> ready) {
                    executor.execute(std::move(task));
                } else {
                    state -> m_continuations.emplace_back(std::move(task));
                }
            //}
            return f;
        }

        template<typename Func>
        auto next(iexecutor& executor, Func&& func) {
            return this -> then(executor, [func = std::forward<Func>(func)](Result<R> x) {
                return func(x.get());
            });
        }

        template<typename Func>
        future<R> recover(iexecutor& executor, Func&& func) {
            return this -> then(executor,
                    [func = std::forward<Func>(func)](Result<R>&& exceptionResult) {
                        try {
                           return exceptionResult.ValueOrThrow();
                        } catch (...) {
                            return func(std::move(Result<R>::Fail(std::current_exception())));
                        }
                    }
            );
        }

        future<R> fallback_to(R fallback) {
            return this -> recover(
                    [fallback = std::move(fallback)](std::exception_ptr) {
                        return fallback;
                    }
            );
        }


        explicit future(std::shared_ptr<shared_state<R>> state): shared_future<R>{state} {}
    private:
        Result<R> nowait_get()  {
            auto state = std::move(this -> m_state);
            if(state -> exceptionPtr)
                return Result<R>::Fail({state -> exceptionPtr});
            return Result<R>::Ok(std::move(state -> value));
        }
    private:
        template<typename U>
        friend class promise;
    };


    template<class R>
    struct shared_future {

        std::shared_ptr<shared_state<R>> m_state { nullptr };
        std::shared_ptr<void> m_cancellable_task_state { nullptr };

        shared_future() = default;
        shared_future(std::shared_ptr<shared_state<R>> s) : m_state(s) {}
        ~shared_future() = default;

        Result<R> get() const {
            wait();
            if (m_state->exceptionPtr) {
                return Result<R>::Fail({m_state->exceptionPtr});
                //std::rethrow_exception(m_state->exception_);
            }
            return Result<R>::Ok(m_state->value);
        }

        bool valid() const {
            return (m_state != nullptr);
        }

        bool ready() const {
            if (m_state == nullptr) return false;
            std::unique_lock<std::mutex> lock(m_state->mutex);
            return m_state->ready;
        }

        void wait() const {
            if (m_state == nullptr) throw "no_state";
            std::unique_lock<std::mutex> lock(m_state->mutex);
            while (!m_state->ready) {
                m_state -> m_cond.wait(lock);
            }
        }

        void attach_cancellable_task_state(std::shared_ptr<void> sptr) {
            m_cancellable_task_state = std::move(sptr);
        }

        template<typename F>
        auto then(iexecutor& iexecutor, F func)
        {
            if (this-> m_state == nullptr) throw "no_state";
            auto sp = this->m_state;
            using R2 = decltype(func(std::declval<Result<R>>()));
            packaged_task<R2()> task(iexecutor, [func = std::move(func), fut = *this]() mutable {
                return func(fut.get());
            });

            future<R2> result = task.get_future();
            std::lock_guard<std::mutex> lock(sp -> mutex);
            if (sp -> ready) {
                iexecutor.execute(std::move(task));
            } else {
                sp -> m_continuations.emplace_back(std::move(task));
            }
            return result;
        }
    };

    template<>
    class future<void>: private shared_future<void> {
    public:
        future() noexcept = default;
        future(const future& other) = delete;
        future& operator=(const future& other) = delete;
        future(future&& other) = default;
        future& operator=(future&& other) = default;
        ~future() noexcept = default;

        using shared_future<void>::attach_cancellable_task_state;

        bool ready() const {
            if(!m_state)
                return false;
            std::lock_guard<std::mutex> lockGuard{m_state -> mutex};
            return m_state -> ready;
        }

        bool valid() const {
            return (m_state != nullptr);
        }

        void wait() const {
            if(!m_state) throw std::runtime_error("no state");
            {
                std::unique_lock<std::mutex> uniqueLock{m_state -> mutex};
                while(!m_state -> ready) {
                    m_state -> m_cond.wait(uniqueLock);
                }
            }
        }

        void get()  {
            wait();
            auto state = std::move(m_state);
            if(state -> exceptionPtr)
                std::rethrow_exception(state -> exceptionPtr);
        }


    private:
        future(std::shared_ptr<shared_state<void>> state): m_state{std::move(state)} {}
    private:
        template<typename U>
        friend class promise;

        std::shared_ptr<shared_state<void>> m_state{nullptr};
        std::shared_ptr<void> m_cancellable_task_state{nullptr};
    };



    template<typename R>
    class promise {
    public:
        promise(): m_state(std::make_shared<shared_state<R>>()){}
        ~promise() {
            abandon_state();
        }

        promise(const promise& other) = delete;
        promise& operator=(const promise& other) = delete;
        promise(promise&& other) = default;
        promise& operator=(promise&& other) noexcept {
            if (this != &other) abandon_state();
            m_state = std::move(other.m_state);
            return *this;
        }

        future<R> get_future() {
            if(!m_state)
                throw std::runtime_error("concurrency::future: nullptr state");
            if(m_future_expired)
                throw std::runtime_error("concurrency::future: future was expired already");

            m_future_expired = true;
            return future<R>(m_state);
        }

        void set_value(R value) {
            if(!m_state)
                return;

            m_state -> value = std::move(value);
            set_ready();
        }

        void set_exception(std::exception_ptr exceptionPtr) {
            if(!m_state)
                return;

            m_state -> exceptionPtr = std::move(exceptionPtr);
            set_ready();
        }
    private:
        promise(iexecutor* executor):
            m_state(std::make_shared<shared_state<R>>()),
            m_executor(executor) {}
    private:
        void set_ready() {
            {
                std::lock_guard<std::mutex> lockGuard{m_state->mutex};
                m_state -> ready = true;
                /// TODO: if has continuations -> chain else if has finish / subscribe -> call directly
                if(!(m_state -> m_continuations.empty())) {
                    for(auto& task: m_state -> m_continuations) {
                        m_executor -> execute(std::move(task));
                    }
                }
                else if(m_state -> m_subscription) {
                    m_state -> m_subscription -> operator()();
                }

                m_state -> m_cond.notify_one();
            }
        }

        void abandon_state() {
            if (m_state != nullptr && !m_state -> ready) {
                set_exception(std::make_exception_ptr("broken_promise"));
            }
        }
    private:
        template<typename U>
        friend class packaged_task;

        std::shared_ptr<shared_state<R>> m_state{nullptr};
        iexecutor* m_executor{nullptr};
        bool m_future_expired{false};
    };

    template<>
    class promise<void> {
    public:
        promise(): m_state(std::make_shared<shared_state<void>>()){}
        ~promise() {
            abandon_state();
        }

        promise(const promise& other) = delete;
        promise& operator=(const promise& other) = delete;
        promise(promise&& other) = default;

        promise& operator=(promise&& other) noexcept {
            if (this != &other) abandon_state();
            m_state = std::move(other.m_state);
            return *this;
        }

        future<void> get_future() {
            if(!m_state)
                throw std::runtime_error("concurrency::future: nullptr state");
            if(m_future_expired)
                throw std::runtime_error("concurrency::future: future was expired already");

            m_future_expired = true;
            return {m_state};
        }

        /*
        void set_value(iexecutor& executor, R value) {
            if(!m_state)
                return;

            m_state -> value = std::move(value);
            set_ready(executor);
        }
        */

        void set_exception(std::exception_ptr exceptionPtr) {
            if(!m_state)
                return;

            m_state -> exceptionPtr = std::move(exceptionPtr);
            set_ready();
        }
    private:
        explicit promise(iexecutor* executor):
                m_state(std::make_shared<shared_state<void>>()),
                m_executor(executor) {}
    private:
        void set_ready() {
            {
                std::lock_guard<std::mutex> lockGuard{m_state->mutex};
                m_state -> ready = true;
                for(auto& task: m_state -> m_continuations) {
                    //executor.execute(std::move(task));
                }
                m_state -> m_cond.notify_one();
            }
        }

        void abandon_state() {
            if (m_state != nullptr && !m_state -> ready) {
                set_exception(std::make_exception_ptr("broken_promise"));
            }
        }

    private:
        std::shared_ptr<shared_state<void>> m_state{nullptr};
        iexecutor* m_executor{nullptr};
        bool m_future_expired{false};
    };

    template<typename R, typename ... Args>
    class packaged_task<R(Args...)> {
    public:
        packaged_task() noexcept = default;
        packaged_task(const packaged_task& other) = delete;
        packaged_task& operator=(const packaged_task& other) = delete;
        packaged_task(packaged_task&& other) = default;
        packaged_task& operator=(packaged_task&& other) noexcept = default;

        template<typename Func,
                typename = std::enable_if_t<!std::is_same_v<packaged_task<R(Args...)>, std::decay_t<Func>>>>
        packaged_task(iexecutor& executor, Func&& func) {
            promise<R> p{&executor};
            m_future = p.get_future();

            auto f_holder = [f = std::forward<Func>(func)]() mutable { return std::move(f); };
            auto s = std::make_shared<decltype(f_holder)>(std::move(f_holder));
            std::weak_ptr<decltype(f_holder)> weak_s = s;
            m_future.attach_cancellable_task_state(s);

            m_task = [p = std::move(p), wptr = std::move(weak_s)](Args&& ... args) mutable {
                if (auto sptr = wptr.lock()) {
                    auto f = (*sptr)();
                    try {
                        auto val = f(std::forward<Args>(args)...);
                        p.set_value(val);
                    } catch (...) {
                        p.set_exception(std::current_exception());
                    }
                }
            };

        }
        ~packaged_task() = default;

        future<R> get_future() {
            if(!m_task) throw std::runtime_error("no state");
            if(!m_future.valid()) throw std::runtime_error("no valid future");
            return std::move(m_future);
        }

        [[nodiscard]]
        bool valid() const {
            return m_task;
        }

        void operator()(Args&& ... args) {
            if(!m_task) throw std::runtime_error("no state");
            if(m_promise_satisfied) throw std::runtime_error("promise already satisfied");
            m_promise_satisfied = true;
            m_task(std::forward<Args>(args)...);
        }
    private:
        rstl::unique_function<void(Args...)> m_task;
        future<R> m_future;
        bool m_promise_satisfied{false};
    };

    template<typename ... Args>
    class packaged_task<void(Args...)>{
    public:
        packaged_task() noexcept = default;
        packaged_task(const packaged_task& other) = delete;
        packaged_task& operator=(const packaged_task& other) = delete;

        packaged_task(packaged_task&& other) = default;
        packaged_task& operator=(packaged_task&& other) noexcept = default;

        template<typename Func,
                typename = std::enable_if_t<!std::is_same_v<packaged_task<void(Args...)>, std::decay_t<Func>>>>
        packaged_task(iexecutor& executor, Func&& func) {
            promise<void> p;
            m_future = p.get_future();

            auto f_holder = [f = std::forward<Func>(func)]() mutable { return std::move(f); };
            auto s = std::make_shared<decltype(f_holder)>(std::move(f_holder));
            std::weak_ptr<decltype(f_holder)> weak_s = s;

            m_future.attach_cancellable_task_state(s);

            m_task = [p = std::move(p), wptr = std::move(weak_s)](Args&& ... args) mutable {
                if (auto sptr = wptr.lock()) {
                    auto f = (*sptr)();
                    try {
                        f(std::forward<Args>(args)...);
                    } catch (...) {
                        p.set_exception(std::current_exception());
                    }
                }
            };
        }

        ~packaged_task() = default;

        future<void> get_future() {
            if(!m_task) throw std::runtime_error("no state");
            if(!m_future.valid()) throw std::runtime_error("no valid future");
            return std::move(m_future);
        }

        [[nodiscard]]
        bool valid() const {
            return m_task;
        }

        void operator()(Args&& ... args) {
            if(!m_task) throw std::runtime_error("no state");
            if(m_promise_satisfied) throw std::runtime_error("promise already satisfied");
            m_promise_satisfied = true;
            m_task(std::forward<Args>(args)...);
        }
    private:
        rstl::unique_function<void(Args...)> m_task;
        future<void> m_future;
        bool m_promise_satisfied{false};
    };


    template<typename Func, typename ... Args>
    auto async(iexecutor& executor, Func&& func, Args&& ... args) -> future<decltype(func(std::forward<Args>(args)...))>
    {
        using R = decltype(func(std::forward<Args>(args)...));

        packaged_task<R(Args...)> task{executor, std::forward<Func>(func)};
        future<R> f = task.get_future();

        auto wrapper = [](packaged_task<R(Args...)>& t, Args&& ... args) {
            t(std::forward<Args>(args)...);
        };

        /// TODO(a.raag) change std::bind to smth more cool !!!!
        auto bound = std::bind(wrapper, std::move(task), std::forward<Args>(args)...);
        executor.execute(std::move(bound));

        return f;
    }

} // namespace concurrency

/// \brief Idea that future can't be using somewhere else, because we work with subscribe / then but not get() method directly.
#define MARK_UNUSABLE(f) std::move(f);
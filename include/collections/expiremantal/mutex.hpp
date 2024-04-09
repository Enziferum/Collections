#pragma once
#include <type_traits>
#include <memory>

namespace collections::rstd {
    namespace priv {
        class mutex_impl;
    }

    class mutex {
    public:
        mutex() = default;
        mutex(const mutex& other) = delete;
        mutex& operator=(const mutex& other) = delete;
        mutex(mutex&& other) = delete;
        mutex& operator=(mutex&& other) = delete;
        ~mutex() noexcept;

        void lock();
        bool try_lock();
        void unlock();
    private:
        std::unique_ptr<priv::mutex_impl> m_mutexImpl;
    };


    template<typename T, typename = std::enable_if_t<std::is_base_of_v<mutex, T>>>
    class lock_guard {
    public:
        explicit lock_guard(const T& mutex): m_mutex{mutex} { m_mutex.lock(); }
        ~lock_guard() noexcept { m_mutex.unlock(); }

        lock_guard(const lock_guard& other) = delete;
        lock_guard& operator=(const lock_guard& other) = delete;
        lock_guard(lock_guard&& other) = delete;
        lock_guard& operator=(lock_guard&& other) = delete;
    private:
        const T& m_mutex;
    };
} // namespace collections::rstd
#pragma once

namespace rstd {
    class mutex {
    public:
        mutex();
        mutex(const mutex& other) = delete;
        mutex& operator=(const mutex& other) = delete;
        mutex(mutex&& other) = delete;
        mutex& operator=(mutex&& other) = delete;
        ~mutex() noexcept;

        void lock();
        void unlock();
    };


    template<typename T>
    class lock_guard {
    public:
        lock_guard(const T& mutex): m_mutex{mutex} { m_mutex.lock(); }
        ~lock_guard() noexcept { m_mutex.unlock(); }

        lock_guard(const lock_guard& other) = delete;
        lock_guard& operator=(const lock_guard& other) = delete;
        lock_guard(lock_guard&& other) = delete;
        lock_guard& operator=(lock_guard&& other) = delete;
    private:
        T m_mutex;
    };
}
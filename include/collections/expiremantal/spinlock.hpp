#pragma once
#include "atomic.hpp"

namespace rstd {
    class spinlock {
    public:
        spinlock();
        spinlock(const spinlock& spinlock) = delete;
        spinlock& operator=(const spinlock& spinlock) = delete;
        spinlock(spinlock&& spinlock) = delete;
        spinlock& operator=(spinlock&& spinlock) = delete;
        ~spinlock() noexcept;

        void lock();
        void unlock();
    private:
        atomic<bool> m_lock;
    };

    /// RAII idiom
    template<typename T>
    class spinguard {
    public:
        spinguard(const T& lock): m_lock{lock} {
            m_lock.lock();
        }

        ~spinguard() noexcept {
            m_lock.unlock();
        }

        spinguard(const spinguard& other) = delete;
        spinguard& operator=(const spinguard& other) = delete;
        spinguard(spinguard&& other) = delete;
        spinguard& operator=(spinguard&& other) = delete;
    private:
        T m_lock;
    };
}
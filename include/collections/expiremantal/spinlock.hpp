#pragma once
#include "atomic.hpp"
#ifndef USE_RSTD_ATOMIC
    #include <atomic>
#endif

namespace collections::rstd {
    /// TTAS ( test && test&set) model
    class spinlock {
    public:
        spinlock() = default;
        spinlock(const spinlock& spinlock) = delete;
        spinlock& operator=(const spinlock& spinlock) = delete;
        spinlock(spinlock&& spinlock) = delete;
        spinlock& operator=(spinlock&& spinlock) = delete;
        ~spinlock() noexcept = default;

        void lock() noexcept {
            for(;;) {
#ifdef USE_RSTD_ATOMIC
                if(!m_lock.exchange(true, memory_order::acquire))
                    return;
                while(m_lock.load(memory_order::relaxed)) {
                    __builtin_ia32_pause();
                }
#else
                if(!m_lock.exchange(true, std::memory_order::memory_order_acquire))
                    return;
                while(m_lock.load(std::memory_order::memory_order_relaxed)) {
                    /// TODO(a.raag): pause on different platforms
#if __has_feature(__builtin_cpu_is)
                    if(__builtin_cpu_is("intel"))
                        __builtin_ia32_pause();
#endif
                }
#endif
            }
        }

        bool try_lock() {
#ifdef USE_RSTD_ATOMIC
            return !m_lock.load(memory_order::relaxed)
                   && !m_lock.exchange(true, memory_order::acquire);
#else
            return !m_lock.load(std::memory_order::memory_order_relaxed)
                   && !m_lock.exchange(true, std::memory_order::memory_order_acquire);
#endif

        }

        void unlock() {
#ifdef USE_RSTD_ATOMIC
            m_lock.store(false, memory_order::release);
#else
            m_lock.store(false, std::memory_order::memory_order_release);
#endif
        }
    private:
#ifdef USE_RSTD_ATOMIC
        atomic<bool> m_lock;
#else
        std::atomic_bool m_lock { false };
#endif
    };

    /// RAII idiom
    template<typename T>
    class spinguard {
    public:
        explicit spinguard(const T& lock) noexcept: m_lock{ lock } {
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
} // namespace collections::rstd
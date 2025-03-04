#pragma once
#include "atomic.hpp"
#ifndef USE_RSTD_ATOMIC
    #include <atomic>
#endif
#if defined(_MSC_VER)
#    include <intrin.h> // _mm_pause()
#elif !defined(__arm__) && !defined(__aarch64__)
#    include <immintrin.h> // _mm_pause()
#endif

namespace collections::rstd {
    /// \brief TTAS ( test && test&set) model
    /// \brief inside futures +25-40% speed vs std::mutex
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
#if defined(__arm__) || defined(__aarch64__) // arm
                    __asm__ __volatile__("yield");
#else
                    /// __asm__ volatile ("pause" ::: "memory"); /// SSE2 support need for CPU
                    _mm_pause();
#endif
                }
#else
                if(!m_lock.exchange(true, std::memory_order::memory_order_acquire))
                    return;
                while(m_lock.load(std::memory_order::memory_order_relaxed)) {
#if defined(__arm__) || defined(__aarch64__) // arm
                    __asm__ __volatile__("yield");
#else
                    /// __asm__ volatile ("pause" ::: "memory"); /// SSE2 support need for CPU
                    _mm_pause();
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
        explicit spinguard(T& lock) noexcept: m_lock{ lock } {
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
        T& m_lock;
    };
} // namespace collections::rstd
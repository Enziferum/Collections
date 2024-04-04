#pragma once
#include "memory_order.hpp"

namespace rstd {

    /// TODO(a.raag)
    /// std::enable_if_t<std::is_pod<T>::value>
    template<typename T>
    class atomic {
    public:
        atomic();
        atomic(const atomic& other) = delete;
        atomic& operator=(const atomic& other) = delete;
        atomic(atomic&& other) = delete;
        atomic& operator=(atomic&& other) = delete;
        ~atomic() noexcept;

        T load(memory_order order = memory_order::seq);
        void store(T value, memory_order order = memory_order::seq);

        bool exchange(T value, memory_order order = memory_order::seq);
        bool compare_exchange_weak(T value, memory_order order = memory_order::seq);
        bool compare_exchange_strong(T value, memory_order order = memory_order::seq);
    private:
        /// todo decay ///
        T value;
    };

    #include "atomic.ipp"
}
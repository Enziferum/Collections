#pragma once

#include <cstddef>

namespace collections::memory {
    template<typename T>
    class LogAllocator {
    public:
        using value_type = T;
        using pointer = T*;

        LogAllocator() = default;
        ~LogAllocator() = default;
        LogAllocator(const LogAllocator& other);


        pointer allocate(std::size_t memSize, const void* hint = nullptr);
        void deallocate(pointer memory, std::size_t deallocMemSize);

        template<typename ...Args>
        void construct(Args&& ...args);

        void destroy(pointer memPointer);

        template<typename U, typename Z>
        friend bool operator==(const LogAllocator<U>& leftAlloc, const LogAllocator<Z>& rightAlloc);

        template<typename U, typename Z>
        friend bool operator!=(const LogAllocator<U>& leftAlloc, const LogAllocator<Z>& rightAlloc);
    };
} // namespace collections::memory
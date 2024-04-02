#pragma once

#include <cstddef>

namespace memory {
    template<typename T>
    class Allocator {
    public:
        using value_type = T;
        using pointer = T*;



        void* allocate(std::size_t memSize, const void* hint = nullptr);

        void deallocate(pointer memory, std::size_t deallocMemSize);
    };
}
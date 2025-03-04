#pragma once

#ifdef USE_TWIST
    #include <twist/ed/stdlike/atomic.hpp>
    #include <twist/ed/stdlike/condition_variable.hpp>
    #include <twist/ed/stdlike/mutex.hpp>
    #include <twist/ed/stdlike/thread.hpp>
#else
    #include <atomic>
    #include <mutex>
    #include <condition_variable>
    #include <thread>
#endif

namespace collections::concurrency {

#ifdef USE_TWIST
    using mutex = twist::ed::stdlike::mutex;
    template<typename T>
    using atomic = twist::ed::stdlike::atomic<T>;
    using condition_variable = twist::ed::stdlike::condition_variable;
    using thread = twist::ed::stdlike::thread;

#else
    using mutex = std::mutex;
    template<typename T>
    using atomic = std::atomic<T>;
    using condition_variable = std::condition_variable;
    using thread = std::thread;
#endif

} // namespace collections::concurrency
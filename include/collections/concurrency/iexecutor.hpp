#pragma once
#include <collections/rstl/unique_function.hpp>

namespace concurrency {

    using Task = rstl::unique_function<void()>;

    class iexecutor {
    public:
        virtual ~iexecutor() = 0;

        virtual void execute(Task task) = 0;
    };
}
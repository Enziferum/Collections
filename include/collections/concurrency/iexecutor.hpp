#pragma once
#include <collections/rstl/unique_function.hpp>

namespace collections::concurrency {

    using Task = rstd::unique_function<void()>;

    class iexecutor {
    public:
        virtual ~iexecutor() = 0;

        virtual void execute(Task task) = 0;
    };
} // namespace collections::rstd
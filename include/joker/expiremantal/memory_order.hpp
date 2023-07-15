#pragma once

namespace rstd {
    enum class memory_order {
        seq,
        acquire,
        release,
        relaxed
    };

}
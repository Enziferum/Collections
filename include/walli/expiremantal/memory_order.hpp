#pragma once

namespace walli::rstd {
    enum class memory_order {
        seq,
        acquire,
        release,
        relaxed
    };

}
#pragma once

namespace collections::rstd {
    enum class memory_order {
        seq,
        acquire,
        release,
        relaxed
    };

}
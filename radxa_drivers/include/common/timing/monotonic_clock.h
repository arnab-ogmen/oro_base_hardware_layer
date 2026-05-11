#pragma once

#include <cstdint>
#include <time.h>

namespace oro {
namespace media {
namespace timing {

inline uint64_t get_monotonic_raw_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

} // namespace timing
} // namespace media
} // namespace oro

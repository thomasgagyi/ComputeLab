#pragma once

#include <chrono>
#include <cstdint>

namespace computelab::timing
{

using HostClock = std::chrono::steady_clock;
using HostTimePoint = HostClock::time_point;

[[nodiscard]] HostTimePoint CaptureHostTime() noexcept;

[[nodiscard]] std::uint64_t ElapsedNanoseconds(
    HostTimePoint begin,
    HostTimePoint end);

} // namespace computelab::timing

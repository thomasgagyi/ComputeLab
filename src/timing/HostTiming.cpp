#include "timing/HostTiming.hpp"

#include <stdexcept>

namespace computelab::timing
{

HostTimePoint CaptureHostTime() noexcept
{
    return HostClock::now();
}

std::uint64_t ElapsedNanoseconds(
    HostTimePoint begin,
    HostTimePoint end)
{
    if (end < begin)
    {
        throw std::invalid_argument("host timing interval end precedes begin");
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin);
    return static_cast<std::uint64_t>(elapsed.count());
}

} // namespace computelab::timing

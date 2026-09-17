#pragma once

#include "timing/HostTiming.hpp"

#include <cstdint>
#include <optional>

namespace computelab::ex2
{

enum class HostTimingStatus
{
    Ok,
    SubmitFailed,
    WaitFailed,
    Timeout,
    Incomplete,
    TimestampInvalid,
};

struct HostTimingIntervals
{
    HostTimingStatus status{HostTimingStatus::Incomplete};
    std::optional<std::uint64_t> hostSubmissionNanoseconds;
    std::optional<std::uint64_t> hostWaitNanoseconds;
    std::optional<std::uint64_t> hostCompletionNanoseconds;
};

// t2 is valid only after a successful completion wait. Failed outcomes may
// retain an observed t0-to-t1 submission interval, but never a wait or
// completion duration.
[[nodiscard]] HostTimingIntervals CalculateHostTimingIntervals(
    HostTimingStatus operationStatus,
    std::optional<timing::HostTimePoint> t0,
    std::optional<timing::HostTimePoint> t1,
    std::optional<timing::HostTimePoint> t2);

} // namespace computelab::ex2

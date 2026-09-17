#include "ex2/Ex2HostTiming.hpp"

#include <chrono>
#include <limits>
#include <type_traits>

namespace computelab::ex2
{
namespace
{

using HostDurationRep = timing::HostClock::duration::rep;

static_assert(std::is_integral_v<HostDurationRep>);
static_assert(std::is_signed_v<HostDurationRep>);

bool CanRepresentHostDuration(
    timing::HostTimePoint begin,
    timing::HostTimePoint end) noexcept
{
    if (end < begin)
    {
        return false;
    }

    const HostDurationRep beginCount = begin.time_since_epoch().count();
    const HostDurationRep endCount = end.time_since_epoch().count();

    if (beginCount < 0 && endCount >= 0)
    {
        const HostDurationRep largestSafeEnd =
            std::numeric_limits<HostDurationRep>::max() + beginCount;
        if (endCount > largestSafeEnd)
        {
            return false;
        }
    }

    const auto elapsed = end - begin;
    const auto elapsedNanoseconds =
        std::chrono::duration<long double, std::nano>{elapsed}.count();

    return elapsedNanoseconds <= static_cast<long double>(
        std::numeric_limits<std::chrono::nanoseconds::rep>::max());
}

std::optional<std::uint64_t> TryElapsedNanoseconds(
    timing::HostTimePoint begin,
    timing::HostTimePoint end)
{
    if (!CanRepresentHostDuration(begin, end))
    {
        return std::nullopt;
    }

    return timing::ElapsedNanoseconds(begin, end);
}

HostTimingIntervals InvalidTimestamps()
{
    return HostTimingIntervals{HostTimingStatus::TimestampInvalid};
}

} // namespace

HostTimingIntervals CalculateHostTimingIntervals(
    HostTimingStatus operationStatus,
    std::optional<timing::HostTimePoint> t0,
    std::optional<timing::HostTimePoint> t1,
    std::optional<timing::HostTimePoint> t2)
{
    if (operationStatus == HostTimingStatus::TimestampInvalid)
    {
        return InvalidTimestamps();
    }

    if (operationStatus == HostTimingStatus::Incomplete)
    {
        return HostTimingIntervals{HostTimingStatus::Incomplete};
    }

    if (operationStatus != HostTimingStatus::Ok)
    {
        if (t2.has_value())
        {
            return InvalidTimestamps();
        }

        if (!t0.has_value() || !t1.has_value())
        {
            return HostTimingIntervals{operationStatus};
        }

        const auto submissionNanoseconds = TryElapsedNanoseconds(*t0, *t1);
        if (!submissionNanoseconds.has_value())
        {
            return InvalidTimestamps();
        }

        return HostTimingIntervals{
            operationStatus,
            submissionNanoseconds,
            std::nullopt,
            std::nullopt};
    }

    if (!t0.has_value() || !t1.has_value())
    {
        return HostTimingIntervals{HostTimingStatus::Incomplete};
    }

    const auto submissionNanoseconds = TryElapsedNanoseconds(*t0, *t1);
    if (!submissionNanoseconds.has_value())
    {
        return InvalidTimestamps();
    }

    if (!t2.has_value())
    {
        return HostTimingIntervals{HostTimingStatus::Incomplete};
    }

    const auto waitNanoseconds = TryElapsedNanoseconds(*t1, *t2);
    const auto completionNanoseconds = TryElapsedNanoseconds(*t0, *t2);
    if (!waitNanoseconds.has_value() || !completionNanoseconds.has_value())
    {
        return InvalidTimestamps();
    }

    if (*submissionNanoseconds >
        std::numeric_limits<std::uint64_t>::max() - *waitNanoseconds)
    {
        return InvalidTimestamps();
    }

    if (*submissionNanoseconds + *waitNanoseconds != *completionNanoseconds)
    {
        return InvalidTimestamps();
    }

    return HostTimingIntervals{
        HostTimingStatus::Ok,
        submissionNanoseconds,
        waitNanoseconds,
        completionNanoseconds};
}

} // namespace computelab::ex2

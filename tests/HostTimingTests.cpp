#include "timing/HostTiming.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <type_traits>

namespace
{

using computelab::timing::CaptureHostTime;
using computelab::timing::ElapsedNanoseconds;
using computelab::timing::HostClock;
using computelab::timing::HostTimePoint;

static_assert(std::is_same_v<HostClock, std::chrono::steady_clock>);
static_assert(HostClock::is_steady);

TEST(HostTiming, UsesSteadyClock)
{
    EXPECT_TRUE(HostClock::is_steady);
}

TEST(HostTiming, IdenticalTimePointsProduceZeroNanoseconds)
{
    const HostTimePoint timePoint{};

    EXPECT_EQ(ElapsedNanoseconds(timePoint, timePoint), 0U);
}

TEST(HostTiming, ConvertsOneMicrosecondToOneThousandNanoseconds)
{
    const HostTimePoint begin{};
    const HostTimePoint end{begin + std::chrono::microseconds{1}};

    EXPECT_EQ(ElapsedNanoseconds(begin, end), 1'000U);
}

TEST(HostTiming, ConvertsOneMillisecondToOneMillionNanoseconds)
{
    const HostTimePoint begin{};
    const HostTimePoint end{begin + std::chrono::milliseconds{1}};

    EXPECT_EQ(ElapsedNanoseconds(begin, end), 1'000'000U);
}

TEST(HostTiming, ConvertsNonRoundDurationToExpectedNanoseconds)
{
    const HostTimePoint begin{};
    const HostTimePoint end{begin + std::chrono::nanoseconds{1'234'567}};

    EXPECT_EQ(ElapsedNanoseconds(begin, end), 1'234'567U);
}

TEST(HostTiming, RejectsReversedInterval)
{
    const HostTimePoint begin{};
    const HostTimePoint end{begin + std::chrono::nanoseconds{1}};

    try
    {
        static_cast<void>(ElapsedNanoseconds(end, begin));
        FAIL() << "expected an invalid_argument";
    }
    catch (const std::invalid_argument& error)
    {
        EXPECT_STREQ("host timing interval end precedes begin", error.what());
    }
}

TEST(HostTiming, LiveCapturesAreNondecreasing)
{
    const HostTimePoint first = CaptureHostTime();
    const HostTimePoint second = CaptureHostTime();

    EXPECT_LE(first, second);
}

TEST(HostTiming, RepeatedConversionIsDeterministic)
{
    const HostTimePoint begin{};
    const HostTimePoint end{begin + std::chrono::nanoseconds{987'654}};
    const std::uint64_t expected = ElapsedNanoseconds(begin, end);

    for (int repetition = 0; repetition < 100; ++repetition)
    {
        EXPECT_EQ(ElapsedNanoseconds(begin, end), expected);
    }
}

} // namespace

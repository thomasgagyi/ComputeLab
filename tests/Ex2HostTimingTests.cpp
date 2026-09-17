#include "ex2/Ex2HostTiming.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <optional>

namespace
{

using computelab::ex2::CalculateHostTimingIntervals;
using computelab::ex2::HostTimingStatus;
using computelab::timing::HostTimePoint;

HostTimePoint AtNanoseconds(std::int64_t nanoseconds)
{
    return HostTimePoint{std::chrono::duration_cast<HostTimePoint::duration>(
        std::chrono::nanoseconds{nanoseconds})};
}

TEST(Ex2HostTiming, CalculatesSubmissionWaitAndCompletionIntervals)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        AtNanoseconds(100),
        AtNanoseconds(140),
        AtNanoseconds(225));

    EXPECT_EQ(intervals.status, HostTimingStatus::Ok);
    ASSERT_TRUE(intervals.hostSubmissionNanoseconds.has_value());
    ASSERT_TRUE(intervals.hostWaitNanoseconds.has_value());
    ASSERT_TRUE(intervals.hostCompletionNanoseconds.has_value());
    EXPECT_EQ(*intervals.hostSubmissionNanoseconds, 40U);
    EXPECT_EQ(*intervals.hostWaitNanoseconds, 85U);
    EXPECT_EQ(*intervals.hostCompletionNanoseconds, 125U);
}

TEST(Ex2HostTiming, CompletionExactlyDecomposesIntoSubmissionAndWait)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        AtNanoseconds(1'000),
        AtNanoseconds(1'123),
        AtNanoseconds(1'999));

    ASSERT_EQ(intervals.status, HostTimingStatus::Ok);
    ASSERT_TRUE(intervals.hostSubmissionNanoseconds.has_value());
    ASSERT_TRUE(intervals.hostWaitNanoseconds.has_value());
    ASSERT_TRUE(intervals.hostCompletionNanoseconds.has_value());
    EXPECT_EQ(
        *intervals.hostSubmissionNanoseconds + *intervals.hostWaitNanoseconds,
        *intervals.hostCompletionNanoseconds);
}

TEST(Ex2HostTiming, EqualTimestampsProduceZeroLengthIntervals)
{
    const HostTimePoint timestamp = AtNanoseconds(500);
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        timestamp,
        timestamp,
        timestamp);

    EXPECT_EQ(intervals.status, HostTimingStatus::Ok);
    EXPECT_EQ(intervals.hostSubmissionNanoseconds, 0U);
    EXPECT_EQ(intervals.hostWaitNanoseconds, 0U);
    EXPECT_EQ(intervals.hostCompletionNanoseconds, 0U);
}

TEST(Ex2HostTiming, AllowsAZeroLengthSubmissionInterval)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        AtNanoseconds(10),
        AtNanoseconds(10),
        AtNanoseconds(17));

    EXPECT_EQ(intervals.status, HostTimingStatus::Ok);
    EXPECT_EQ(intervals.hostSubmissionNanoseconds, 0U);
    EXPECT_EQ(intervals.hostWaitNanoseconds, 7U);
    EXPECT_EQ(intervals.hostCompletionNanoseconds, 7U);
}

TEST(Ex2HostTiming, AllowsAZeroLengthWaitInterval)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        AtNanoseconds(10),
        AtNanoseconds(17),
        AtNanoseconds(17));

    EXPECT_EQ(intervals.status, HostTimingStatus::Ok);
    EXPECT_EQ(intervals.hostSubmissionNanoseconds, 7U);
    EXPECT_EQ(intervals.hostWaitNanoseconds, 0U);
    EXPECT_EQ(intervals.hostCompletionNanoseconds, 7U);
}

TEST(Ex2HostTiming, RejectsT1BeforeT0)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        AtNanoseconds(20),
        AtNanoseconds(19),
        AtNanoseconds(30));

    EXPECT_EQ(intervals.status, HostTimingStatus::TimestampInvalid);
    EXPECT_FALSE(intervals.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

TEST(Ex2HostTiming, RejectsT2BeforeT1)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        AtNanoseconds(10),
        AtNanoseconds(30),
        AtNanoseconds(29));

    EXPECT_EQ(intervals.status, HostTimingStatus::TimestampInvalid);
    EXPECT_FALSE(intervals.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

TEST(Ex2HostTiming, MissingSuccessfulTimestampIsExplicitlyIncomplete)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        AtNanoseconds(10),
        AtNanoseconds(20),
        std::nullopt);

    EXPECT_EQ(intervals.status, HostTimingStatus::Incomplete);
    EXPECT_FALSE(intervals.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

TEST(Ex2HostTiming, SubmitFailureNeverProducesWaitOrCompletionDuration)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::SubmitFailed,
        AtNanoseconds(10),
        AtNanoseconds(12),
        std::nullopt);

    EXPECT_EQ(intervals.status, HostTimingStatus::SubmitFailed);
    EXPECT_EQ(intervals.hostSubmissionNanoseconds, 2U);
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

TEST(Ex2HostTiming, WaitFailureNeverProducesWaitOrCompletionDuration)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::WaitFailed,
        AtNanoseconds(10),
        AtNanoseconds(12),
        std::nullopt);

    EXPECT_EQ(intervals.status, HostTimingStatus::WaitFailed);
    EXPECT_EQ(intervals.hostSubmissionNanoseconds, 2U);
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

TEST(Ex2HostTiming, TimeoutNeverProducesWaitOrCompletionDuration)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Timeout,
        AtNanoseconds(10),
        AtNanoseconds(12),
        std::nullopt);

    EXPECT_EQ(intervals.status, HostTimingStatus::Timeout);
    EXPECT_EQ(intervals.hostSubmissionNanoseconds, 2U);
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

TEST(Ex2HostTiming, FailureWithoutCompleteCaptureRetainsFailureStatus)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::SubmitFailed,
        AtNanoseconds(10),
        std::nullopt,
        std::nullopt);

    EXPECT_EQ(intervals.status, HostTimingStatus::SubmitFailed);
    EXPECT_FALSE(intervals.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

TEST(Ex2HostTiming, FailedOutcomeRejectsAFabricatedT2)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::WaitFailed,
        AtNanoseconds(10),
        AtNanoseconds(12),
        AtNanoseconds(20));

    EXPECT_EQ(intervals.status, HostTimingStatus::TimestampInvalid);
    EXPECT_FALSE(intervals.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

TEST(Ex2HostTiming, ExplicitIncompleteOutcomeContainsNoDurations)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Incomplete,
        AtNanoseconds(10),
        std::nullopt,
        std::nullopt);

    EXPECT_EQ(intervals.status, HostTimingStatus::Incomplete);
    EXPECT_FALSE(intervals.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

TEST(Ex2HostTiming, AcceptsIntervalsAtThePositiveClockBoundary)
{
    const HostTimePoint t2 = HostTimePoint::max();
    const HostTimePoint t1 = t2 - std::chrono::nanoseconds{1};
    const HostTimePoint t0 = t1 - std::chrono::nanoseconds{1};
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        t0,
        t1,
        t2);

    EXPECT_EQ(intervals.status, HostTimingStatus::Ok);
    EXPECT_EQ(intervals.hostSubmissionNanoseconds, 1U);
    EXPECT_EQ(intervals.hostWaitNanoseconds, 1U);
    EXPECT_EQ(intervals.hostCompletionNanoseconds, 2U);
}

TEST(Ex2HostTiming, RejectsAnUnrepresentableClockSpan)
{
    const auto intervals = CalculateHostTimingIntervals(
        HostTimingStatus::Ok,
        HostTimePoint::min(),
        HostTimePoint{},
        HostTimePoint::max());

    EXPECT_EQ(intervals.status, HostTimingStatus::TimestampInvalid);
    EXPECT_FALSE(intervals.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(intervals.hostCompletionNanoseconds.has_value());
}

} // namespace

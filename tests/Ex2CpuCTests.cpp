#include "ex2/Ex2CpuOracles.hpp"

#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace
{

TEST(Ex2CpuC, ApprovedContentionConstantsAreExact)
{
    EXPECT_EQ(computelab::ex2::ContentionElementCount, 1'048'576U);
    EXPECT_EQ(computelab::ex2::ContentionActiveAll, 1'048'576U);
    EXPECT_EQ(computelab::ex2::ContentionActiveOnePer32, 32'768U);
    EXPECT_EQ(computelab::ex2::ContentionActive64, 64U);
}

TEST(Ex2CpuC, SmallTargetsAndHistogramMatchManualEnumeration)
{
    const auto reference = computelab::ex2::ReferenceContention(8U, 2U);
    EXPECT_EQ(reference.targets,
        (std::vector<std::uint32_t>{1U, 0U, 1U, 0U, 1U, 0U, 1U, 0U}));
    EXPECT_EQ(reference.counters,
        (std::vector<std::uint32_t>{4U, 4U, 0U, 0U, 0U, 0U, 0U, 0U}));
    EXPECT_EQ(reference.activeCounterCount, 2U);
    EXPECT_TRUE(computelab::ex2::ValidateContentionResult(
        reference, reference.counters));
    EXPECT_EQ(computelab::ex2::WordInputSha256(reference.counters),
        "ab99f1d6520dd996faceba083609eb56c02d53615f851b82c1558cf9cf0e7d22");
}

TEST(Ex2CpuC, SmallAnalogsOfAllApprovedContentionLevelsAreBalanced)
{
    constexpr std::uint64_t elementCount = 128U;
    for (const std::uint64_t activeCounterCount
        : {elementCount, elementCount / 32U, 64ULL})
    {
        const auto reference = computelab::ex2::ReferenceContention(
            elementCount, activeCounterCount);
        ASSERT_EQ(reference.targets.size(), elementCount);
        ASSERT_EQ(reference.counters.size(), elementCount);
        for (std::size_t index = 0U; index < reference.counters.size(); ++index)
        {
            EXPECT_EQ(reference.counters[index],
                index < activeCounterCount
                    ? elementCount / activeCounterCount
                    : 0U);
        }
        const std::uint64_t total = std::accumulate(
            reference.counters.begin(), reference.counters.end(), 0ULL);
        EXPECT_EQ(total, elementCount);
    }
}

TEST(Ex2CpuC, ZeroUpdateCorrectnessCaseHasExplicitEmptyResetState)
{
    const auto reference = computelab::ex2::ReferenceContention(0U, 0U);
    EXPECT_TRUE(reference.targets.empty());
    EXPECT_TRUE(reference.counters.empty());
    EXPECT_TRUE(computelab::ex2::ValidateContentionResult(reference, {}));
}

TEST(Ex2CpuC, RepeatedReferenceConstructionIsDeterministic)
{
    const auto first = computelab::ex2::ReferenceContention(128U, 64U);
    static_cast<void>(computelab::ex2::ReferenceContention(8U, 2U));
    const auto second = computelab::ex2::ReferenceContention(128U, 64U);
    EXPECT_EQ(first.targets, second.targets);
    EXPECT_EQ(first.counters, second.counters);
}

TEST(Ex2CpuC, RejectsMalformedTargetsParametersAndOverflowingCounts)
{
    const std::vector<std::uint32_t> oneTarget{0U};
    const std::vector<std::uint32_t> outOfRange{0U, 2U};
    EXPECT_THROW(static_cast<void>(
        computelab::ex2::ReferenceContentionHistogram(oneTarget, 0U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        computelab::ex2::ReferenceContentionHistogram(outOfRange, 2U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        computelab::ex2::GenerateContentionTargets(8U, 0U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        computelab::ex2::GenerateContentionTargets(8U, 3U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        computelab::ex2::GenerateContentionTargets(8191U, 1U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        computelab::ex2::GenerateContentionTargets(
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) + 1U, 1U)),
        std::overflow_error);
}

TEST(Ex2CpuC, ValidationDetectsWrongInactiveAndTruncatedCounters)
{
    const auto reference = computelab::ex2::ReferenceContention(8U, 2U);
    auto wrong = reference.counters;
    wrong[7] = 1U;
    EXPECT_FALSE(computelab::ex2::ValidateContentionResult(reference, wrong));
    EXPECT_FALSE(computelab::ex2::ValidateContentionResult(reference,
        std::span<const std::uint32_t>{wrong.data(), wrong.size() - 1U}));
}

} // namespace

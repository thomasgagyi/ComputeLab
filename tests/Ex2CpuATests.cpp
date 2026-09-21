#include "ex2/Ex2CpuOracles.hpp"

#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{

TEST(Ex2CpuOracleBounds, RejectsOversizedLogicalCountsWithoutAllocation)
{
    EXPECT_NO_THROW(computelab::ex2::ValidateUint32IndexedOracleElementCount(
        std::numeric_limits<std::uint32_t>::max()));
    EXPECT_THROW(computelab::ex2::ValidateUint32IndexedOracleElementCount(
        static_cast<std::uint64_t>(std::numeric_limits<std::uint32_t>::max())
            + 1U),
        std::invalid_argument);
}

TEST(Ex2CpuA, ReproducesLiteralA1AndA2SpecificationFixtures)
{
    const std::vector<std::uint32_t> input{
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};

    EXPECT_EQ(computelab::ex2::ReferenceA1(input),
        (std::vector<std::uint32_t>{
            0x3AB8D324U, 0x5D43B3A0U, 0xA5DBB319U, 0x0F1AB744U}));
    EXPECT_EQ(computelab::ex2::ReferenceA2(input),
        (std::vector<std::uint32_t>{
            0x4579A7C6U, 0x6D06CDCFU, 0x3BE3E55EU, 0x271DAF20U}));
}

TEST(Ex2CpuA, UsesExactUnsignedWraparound)
{
    EXPECT_EQ(computelab::ex2::TransformA1Value(
        0xFFFFFFFFU, 0xFFFFFFFFU), 0x61C88647U);
    EXPECT_EQ(computelab::ex2::TransformA2Value(
        0xFFFFFFFFU, 0xFFFFFFFFU), 0xA44391AAU);
}

TEST(Ex2CpuA, EmptyAndIrregularInputsPreserveLengthAndSource)
{
    EXPECT_TRUE(computelab::ex2::ReferenceA1({}).empty());
    EXPECT_TRUE(computelab::ex2::ReferenceA2({}).empty());

    const auto input = computelab::ex2::GenerateWordInput(
        computelab::ex2::CoreInputSeed, 257U);
    const auto original = input;
    const auto a1 = computelab::ex2::ReferenceA1(input);
    const auto a2 = computelab::ex2::ReferenceA2(input);

    EXPECT_EQ(input, original);
    EXPECT_EQ(a1.size(), input.size());
    EXPECT_EQ(a2.size(), input.size());
}

TEST(Ex2CpuA, RepeatedCallsAndCallOrderDoNotChangeResults)
{
    const auto input = computelab::ex2::GenerateWordInput(
        computelab::ex2::CoreInputSeed, 17U);
    const auto a2First = computelab::ex2::ReferenceA2(input);
    const auto a1 = computelab::ex2::ReferenceA1(input);
    const auto a2Second = computelab::ex2::ReferenceA2(input);

    EXPECT_EQ(a2First, a2Second);
    EXPECT_EQ(a1, computelab::ex2::ReferenceA1(input));
}

TEST(Ex2CpuA, CanonicalExpectedOutputDigestsAreStable)
{
    const auto input = computelab::ex2::GenerateWordInput(
        computelab::ex2::CoreInputSeed, 4U);
    EXPECT_EQ(computelab::ex2::WordInputSha256(
        computelab::ex2::ReferenceA1(input)),
        "003aad6678af8e5bc0d3e0f6872657d9d3be0cf4f4384071aa9363ab7e0bd6ef");
    EXPECT_EQ(computelab::ex2::WordInputSha256(
        computelab::ex2::ReferenceA2(input)),
        "e573d986fee60cebc1695797c0592ac60fd539739d2c630b194e41c397180c9a");
}

} // namespace

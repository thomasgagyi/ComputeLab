#include "ex2/Ex2CpuOracles.hpp"

#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{

void ExpectPermutation(const std::vector<std::uint32_t>& indices)
{
    std::vector<std::uint32_t> sorted = indices;
    std::sort(sorted.begin(), sorted.end());
    for (std::size_t index = 0U; index < sorted.size(); ++index)
    {
        EXPECT_EQ(sorted[index], index);
    }
}

TEST(Ex2CpuB, PatternRevisionIdentifiersAreExact)
{
    EXPECT_EQ(computelab::ex2::StructuredIndexRevision, "structured-v1");
    EXPECT_EQ(computelab::ex2::ShuffledIndexRevision, "shuffled-v1");
}

TEST(Ex2CpuB, StructuredAndShuffledSmallPermutationsMatchIndependentFixtures)
{
    EXPECT_EQ(computelab::ex2::GenerateStructuredPermutation(4U),
        (std::vector<std::uint32_t>{1U, 0U, 3U, 2U}));
    EXPECT_EQ(computelab::ex2::GenerateShuffledPermutation(
        computelab::ex2::CoreInputSeed, 4U),
        (std::vector<std::uint32_t>{3U, 0U, 2U, 1U}));
}

TEST(Ex2CpuB, GeneratedPermutationsAreDeterministicUniqueAndInRange)
{
    const auto structured = computelab::ex2::GenerateStructuredPermutation(257U);
    const auto shuffled = computelab::ex2::GenerateShuffledPermutation(
        computelab::ex2::CoreInputSeed, 257U);
    ExpectPermutation(structured);
    ExpectPermutation(shuffled);
    EXPECT_EQ(structured,
        computelab::ex2::GenerateStructuredPermutation(257U));
    EXPECT_EQ(shuffled, computelab::ex2::GenerateShuffledPermutation(
        computelab::ex2::CoreInputSeed, 257U));
}

TEST(Ex2CpuB, ZeroSizedPermutationsAndOperationsAreExplicitlyEmpty)
{
    const std::vector<std::uint32_t> empty;
    EXPECT_TRUE(computelab::ex2::GenerateStructuredPermutation(0U).empty());
    EXPECT_TRUE(computelab::ex2::GenerateShuffledPermutation(
        computelab::ex2::CoreInputSeed, 0U).empty());
    EXPECT_NO_THROW(computelab::ex2::ValidateIndexPermutation(empty, 0U));
    EXPECT_TRUE(computelab::ex2::ReferenceB1Gather(empty, empty).empty());
    EXPECT_TRUE(computelab::ex2::ReferenceB2Scatter(empty, empty).empty());
}

TEST(Ex2CpuB, StructuredGatherAndScatterMatchLiteralSpecificationFixtures)
{
    const auto input = computelab::ex2::GenerateWordInput(
        computelab::ex2::CoreInputSeed, 4U);
    const auto original = input;
    const auto indices = computelab::ex2::GenerateStructuredPermutation(4U);
    const auto originalIndices = indices;

    const auto gather = computelab::ex2::ReferenceB1Gather(input, indices);
    const auto scatter = computelab::ex2::ReferenceB2Scatter(input, indices);
    EXPECT_EQ(gather, (std::vector<std::uint32_t>{
        0x5D43B3A3U, 0x3AB8D327U, 0x0F1AB743U, 0xA5DBB31EU}));
    EXPECT_EQ(scatter, (std::vector<std::uint32_t>{
        0x5D43B3A0U, 0x3AB8D324U, 0x0F1AB744U, 0xA5DBB319U}));
    EXPECT_EQ(input, original);
    EXPECT_EQ(indices, originalIndices);
    EXPECT_EQ(computelab::ex2::WordInputSha256(gather),
        "9532ffadf8f1330621ac0d695c56d5ff54411837f4b1a30fd0e0de47886bf538");
    EXPECT_EQ(computelab::ex2::WordInputSha256(scatter),
        "f282b956af7b8845af2227ea5ecf40150257a8114500d17fda183faa6c98f26c");
}

TEST(Ex2CpuB, ShuffledGatherAndScatterMatchIndependentManualMapping)
{
    const auto input = computelab::ex2::GenerateWordInput(
        computelab::ex2::CoreInputSeed, 4U);
    const auto indices = computelab::ex2::GenerateShuffledPermutation(
        computelab::ex2::CoreInputSeed, 4U);
    EXPECT_EQ(computelab::ex2::ReferenceB1Gather(input, indices),
        (std::vector<std::uint32_t>{
            0x0F1AB741U, 0x3AB8D327U, 0xA5DBB319U, 0x5D43B3A6U}));
    EXPECT_EQ(computelab::ex2::ReferenceB2Scatter(input, indices),
        (std::vector<std::uint32_t>{
            0x5D43B3A0U, 0x0F1AB744U, 0xA5DBB319U, 0x3AB8D324U}));
}

TEST(Ex2CpuB, RejectsMalformedOrUnrepresentableIndicesBeforeExecution)
{
    const std::vector<std::uint32_t> duplicate{0U, 0U};
    const std::vector<std::uint32_t> outOfRange{0U, 2U};
    const std::vector<std::uint32_t> incomplete{0U};
    const std::vector<std::uint32_t> input{1U, 2U};
    EXPECT_THROW(computelab::ex2::ValidateIndexPermutation(duplicate, 2U),
        std::invalid_argument);
    EXPECT_THROW(computelab::ex2::ValidateIndexPermutation(outOfRange, 2U),
        std::invalid_argument);
    EXPECT_THROW(computelab::ex2::ValidateIndexPermutation(incomplete, 2U),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(computelab::ex2::ReferenceB1Gather(
        input, incomplete)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(computelab::ex2::ReferenceB2Scatter(
        input, duplicate)), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        computelab::ex2::GenerateStructuredPermutation(8191U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        computelab::ex2::GenerateShuffledPermutation(0U,
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) + 1U)),
        std::invalid_argument);
}

} // namespace

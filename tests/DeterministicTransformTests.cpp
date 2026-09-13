#include "input/SeededInput.hpp"
#include "oracle/DeterministicTransform.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

TEST(DeterministicTransform, ZeroLengthInputProducesEmptyOutput)
{
    EXPECT_TRUE(computelab::TransformSequence({}).empty());
}

TEST(DeterministicTransform, OneElementInputUsesZeroBasedIndex)
{
    EXPECT_EQ(
        computelab::TransformSequence({1U}),
        (std::vector<std::uint32_t>{1364076727U}));
}

TEST(DeterministicTransform, PerElementGoldenVectorsMatchExactly)
{
    EXPECT_EQ(computelab::TransformValue(0U, 0U), 0U);
    EXPECT_EQ(computelab::TransformValue(1U, 0U), 1364076727U);
    EXPECT_EQ(computelab::TransformValue(4294967295U, 0U), 2180083513U);
    EXPECT_EQ(computelab::TransformValue(305419896U, 7U), 3024703442U);
    EXPECT_EQ(computelab::TransformValue(3405691582U, 42U), 1125790243U);
}

TEST(DeterministicTransform, SeededGeneratorRepresentativeSequenceMatchesGoldenOutput)
{
    const auto input = computelab::GenerateSeededInput(0U, 8U);

    EXPECT_EQ(
        computelab::TransformSequence(input),
        (std::vector<std::uint32_t>{
            4095279456U, 2420367307U, 825526943U, 1765017138U,
            1544646916U, 2351693811U, 1804470722U, 772941641U}));
}

TEST(DeterministicTransform, InputSequenceRemainsUnchanged)
{
    const std::vector<std::uint32_t> input{
        11U, 22U, 33U, 44U, 55U};
    const auto original = input;

    static_cast<void>(computelab::TransformSequence(input));

    EXPECT_EQ(input, original);
}

TEST(DeterministicTransform, IdenticalValuesAtDifferentPositionsUseTheirLogicalIndex)
{
    const std::vector<std::uint32_t> input{
        2779096485U, 2779096485U, 2779096485U, 2779096485U,
        2779096485U, 2779096485U, 2779096485U, 2779096485U};

    EXPECT_EQ(
        computelab::TransformSequence(input),
        (std::vector<std::uint32_t>{
            91894882U, 2425224017U, 1901078733U, 2336552356U,
            1654752222U, 1131494299U, 2697015364U, 3219097604U}));
}

TEST(DeterministicTransform, RepeatedCallsAreExactlyDeterministic)
{
    const auto input = computelab::GenerateSeededInput(0xDEADBEEFU, 32U);

    const auto first = computelab::TransformSequence(input);
    const auto second = computelab::TransformSequence(input);

    EXPECT_EQ(first, second);
}

} // namespace

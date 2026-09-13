#include "input/SeededInput.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

namespace
{

TEST(SeededInput, ZeroLengthIsEmpty)
{
    EXPECT_TRUE(computelab::GenerateSeededInput(42U, 0U).empty());
}

TEST(SeededInput, RepeatedCallsWithSameArgumentsMatchExactly)
{
    const auto first = computelab::GenerateSeededInput(0x0123456789ABCDEFULL, 32U);
    const auto second = computelab::GenerateSeededInput(0x0123456789ABCDEFULL, 32U);

    EXPECT_EQ(first, second);
}

TEST(SeededInput, FixedSeedsMatchVersionOneGoldenValues)
{
    EXPECT_EQ(
        computelab::GenerateSeededInput(0U, 8U),
        (std::vector<std::uint32_t>{
            3410091070U, 1044445579U, 4029868217U, 3471047918U,
            990324140U, 4250921454U, 69919023U, 1166165736U}));

    EXPECT_EQ(
        computelab::GenerateSeededInput(1U, 8U),
        (std::vector<std::uint32_t>{
            3144183656U, 588839502U, 2061911450U, 2033565838U,
            3975964472U, 3113445449U, 2438713780U, 468798217U}));

    EXPECT_EQ(
        computelab::GenerateSeededInput(0xDEADBEEFU, 8U),
        (std::vector<std::uint32_t>{
            333615041U, 475256991U, 2047326476U, 752301302U,
            2262885350U, 4271618495U, 875810243U, 2898533727U}));
}

TEST(SeededInput, DifferentSeedsProduceDifferentRepresentativeSequences)
{
    EXPECT_NE(
        computelab::GenerateSeededInput(17U, 32U),
        computelab::GenerateSeededInput(18U, 32U));
}

TEST(SeededInput, PrefixIsStableAcrossRequestedLengths)
{
    constexpr std::size_t prefixCount = 7U;
    const auto prefix = computelab::GenerateSeededInput(99U, prefixCount);
    const auto longer = computelab::GenerateSeededInput(99U, 19U);

    ASSERT_LT(prefixCount, longer.size());
    EXPECT_EQ(prefix, std::vector<std::uint32_t>(longer.begin(), longer.begin() + prefixCount));
}

} // namespace

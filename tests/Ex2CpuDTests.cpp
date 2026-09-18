#include "ex2/Ex2CpuOracles.hpp"

#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{

const std::vector<std::uint32_t> kInitial{
    0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};

TEST(Ex2CpuD, KZeroIsIdentityInStateA)
{
    const auto original = kInitial;
    const auto reference = computelab::ex2::ReferenceD1(kInitial, 0U);
    EXPECT_EQ(reference.finalState, kInitial);
    EXPECT_EQ(reference.finalBuffer,
        computelab::ex2::IterativeFinalBuffer::StateA);
    EXPECT_EQ(kInitial, original);
}

TEST(Ex2CpuD, KOneAndKTwoMatchLiteralSpecificationFixturesAndParity)
{
    const auto kOne = computelab::ex2::ReferenceD1(kInitial, 1U);
    EXPECT_EQ(kOne.finalState, (std::vector<std::uint32_t>{
        0xD664E09CU, 0x27C0F020U, 0x3AC0DF49U, 0x62A16496U}));
    EXPECT_EQ(kOne.finalBuffer,
        computelab::ex2::IterativeFinalBuffer::StateB);

    const auto kTwo = computelab::ex2::ReferenceD1(kInitial, 2U);
    EXPECT_EQ(kTwo.finalState, (std::vector<std::uint32_t>{
        0x89BDA0DEU, 0xBE3BAF8CU, 0x1E3F5AC9U, 0x120E2194U}));
    EXPECT_EQ(kTwo.finalBuffer,
        computelab::ex2::IterativeFinalBuffer::StateA);
    EXPECT_EQ(computelab::ex2::WordInputSha256(kOne.finalState),
        "b7acadefadf2d0ad99fc5ace0c327d25795d6b94e5866e71aaf66383efa34da3");
    EXPECT_EQ(computelab::ex2::WordInputSha256(kTwo.finalState),
        "dc089b984646d3c30e141ec9f377f5b7596f5feaacf0caf08cad7bf50aafe1b7");
}

TEST(Ex2CpuD, K16AndK64MatchIndependentSingleElementFixtures)
{
    const std::vector<std::uint32_t> input{0xA48FAA9DU};
    EXPECT_EQ(computelab::ex2::ReferenceD1(input, 16U).finalState,
        (std::vector<std::uint32_t>{0x8B2A8389U}));
    EXPECT_EQ(computelab::ex2::ReferenceD1(input, 64U).finalState,
        (std::vector<std::uint32_t>{0xB53390FEU}));
}

TEST(Ex2CpuD, PerPassTransformUsesExactUnsignedWraparound)
{
    EXPECT_EQ(computelab::ex2::TransformD1Value(
        0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU), 0xB85B4521U);
}

TEST(Ex2CpuD, SeededReferenceResetsAndRegeneratesInitialState)
{
    const auto first = computelab::ex2::ReferenceD1(
        computelab::ex2::CoreInputSeed, 4U, 2U);
    static_cast<void>(computelab::ex2::ReferenceD1(
        computelab::ex2::CoreInputSeed, 4U, 1U));
    const auto second = computelab::ex2::ReferenceD1(
        computelab::ex2::CoreInputSeed, 4U, 2U);
    EXPECT_EQ(first.finalState, second.finalState);
    EXPECT_EQ(first.finalState,
        computelab::ex2::ReferenceD1(kInitial, 2U).finalState);
}

TEST(Ex2CpuD, EmptyStateTracksParityWithoutInventingElements)
{
    const std::vector<std::uint32_t> empty;
    EXPECT_TRUE(computelab::ex2::ReferenceD1(empty, 0U).finalState.empty());
    const auto onePass = computelab::ex2::ReferenceD1(empty, 1U);
    EXPECT_TRUE(onePass.finalState.empty());
    EXPECT_EQ(onePass.finalBuffer,
        computelab::ex2::IterativeFinalBuffer::StateB);
}

TEST(Ex2CpuD, RejectsIterationCountsOutsideUint32PassSemantics)
{
    EXPECT_THROW(static_cast<void>(computelab::ex2::ReferenceD1(
        kInitial,
        static_cast<std::uint64_t>(
            std::numeric_limits<std::uint32_t>::max()) + 1U)),
        std::invalid_argument);
}

} // namespace

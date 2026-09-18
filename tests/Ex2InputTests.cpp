#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace
{

using computelab::ex2::Byte;
using computelab::ex2::CoreInputSeed;
using computelab::ex2::Mix;
using computelab::ex2::Value;

static_assert(std::is_same_v<decltype(Mix(0U, 0U)), std::uint64_t>);
static_assert(std::is_same_v<decltype(Value(0U, 0U)), std::uint32_t>);
static_assert(std::is_same_v<decltype(Byte(0U, 0U)), std::uint8_t>);

TEST(Ex2InputGenerator, RevisionAndCoreSeedAreExact)
{
    EXPECT_EQ(computelab::ex2::InputGeneratorRevision, "ex2-mix64-v1");
    EXPECT_EQ(CoreInputSeed, 0x0123456789ABCDEFULL);
}

TEST(Ex2InputGenerator, MatchesIndependentSpecificationFixtures)
{
    struct Fixture
    {
        std::uint64_t mix;
        std::uint32_t value;
        std::uint8_t byte;
    };
    constexpr std::array fixtures{
        Fixture{0x157A3807A48FAA9DULL, 0xA48FAA9DU, 0x9DU},
        Fixture{0x9804297CC374CA1AULL, 0xC374CA1AU, 0x1AU},
        Fixture{0xAF8D95523BECCAA2ULL, 0x3BECCAA2U, 0xA2U},
        Fixture{0xCB4E5F6A912DCEF8ULL, 0x912DCEF8U, 0xF8U}};

    for (std::uint64_t index = 0U; index < fixtures.size(); ++index)
    {
        const auto& fixture = fixtures[static_cast<std::size_t>(index)];
        EXPECT_EQ(Mix(CoreInputSeed, index), fixture.mix);
        EXPECT_EQ(Value(CoreInputSeed, index), fixture.value);
        EXPECT_EQ(Byte(CoreInputSeed, index), fixture.byte);
    }
}

TEST(Ex2InputGenerator, IsIndependentOfCallOrderAndRepeatedCalls)
{
    const std::uint64_t valueThree = Mix(CoreInputSeed, 3U);
    const std::uint64_t valueZero = Mix(CoreInputSeed, 0U);
    const std::uint64_t valueTwo = Mix(CoreInputSeed, 2U);
    const std::uint64_t valueOne = Mix(CoreInputSeed, 1U);

    EXPECT_EQ(valueZero, 0x157A3807A48FAA9DULL);
    EXPECT_EQ(valueOne, 0x9804297CC374CA1AULL);
    EXPECT_EQ(valueTwo, 0xAF8D95523BECCAA2ULL);
    EXPECT_EQ(valueThree, 0xCB4E5F6A912DCEF8ULL);
    EXPECT_EQ(Mix(CoreInputSeed, 3U), valueThree);
    EXPECT_EQ(Mix(CoreInputSeed, 0U), valueZero);
}

TEST(Ex2InputGenerator, CoversUnsignedSeedAndIndexBoundaries)
{
    constexpr std::uint64_t maximum =
        std::numeric_limits<std::uint64_t>::max();

    EXPECT_EQ(Mix(0U, 0U), 0xE220A8397B1DCDAFULL);
    EXPECT_EQ(Mix(maximum, 0U), 0xE4D971771B652C20ULL);
    EXPECT_EQ(Mix(maximum, 1U), Mix(0U, 0U));
    EXPECT_EQ(Mix(CoreInputSeed, 0x1'0000'0000ULL),
        0x02CD66BF5AD4FA44ULL);
    EXPECT_NE(Mix(CoreInputSeed, 0x1'0000'0000ULL),
        Mix(CoreInputSeed, 0U));
}

TEST(Ex2InputGenerator, WordAndByteComeFromTheSameMixBits)
{
    for (std::uint64_t index = 0U; index < 4U; ++index)
    {
        const std::uint64_t mixed = Mix(CoreInputSeed, index);
        EXPECT_EQ(Value(CoreInputSeed, index),
            static_cast<std::uint32_t>(mixed & 0xFFFFFFFFULL));
        EXPECT_EQ(Byte(CoreInputSeed, index),
            static_cast<std::uint8_t>(mixed & 0xFFULL));
        EXPECT_EQ(static_cast<std::uint8_t>(Value(CoreInputSeed, index)),
            Byte(CoreInputSeed, index));
    }
}

TEST(Ex2InputBuffers, GenerateExactLengthsAndContents)
{
    for (const std::uint64_t count : {0ULL, 1ULL, 4ULL, 257ULL})
    {
        const auto words = computelab::ex2::GenerateWordInput(
            CoreInputSeed, count);
        const auto bytes = computelab::ex2::GenerateByteInput(
            CoreInputSeed, count);
        ASSERT_EQ(words.size(), count);
        ASSERT_EQ(bytes.size(), count);
        for (std::uint64_t index = 0U; index < count; ++index)
        {
            EXPECT_EQ(words[static_cast<std::size_t>(index)],
                Value(CoreInputSeed, index));
            EXPECT_EQ(bytes[static_cast<std::size_t>(index)],
                Byte(CoreInputSeed, index));
        }
    }
}

TEST(Ex2InputBuffers, RepeatedGenerationIsByteForByteIdentical)
{
    const auto firstWords = computelab::ex2::GenerateWordInput(
        CoreInputSeed, 257U);
    const auto secondWords = computelab::ex2::GenerateWordInput(
        CoreInputSeed, 257U);
    const auto firstBytes = computelab::ex2::GenerateByteInput(
        CoreInputSeed, 257U);
    const auto secondBytes = computelab::ex2::GenerateByteInput(
        CoreInputSeed, 257U);

    EXPECT_EQ(firstWords, secondWords);
    EXPECT_EQ(firstBytes, secondBytes);
}

TEST(Ex2InputBuffers, ExcessiveLogicalCountsFailBeforeAllocation)
{
    constexpr std::uint64_t excessive =
        std::numeric_limits<std::uint64_t>::max();
    EXPECT_THROW(
        static_cast<void>(
            computelab::ex2::GenerateWordInput(CoreInputSeed, excessive)),
        std::length_error);
    EXPECT_THROW(
        static_cast<void>(
            computelab::ex2::GenerateByteInput(CoreInputSeed, excessive)),
        std::length_error);
}

TEST(Ex2InputBytes, WordEncodingIsExplicitLittleEndian)
{
    const std::vector<std::uint32_t> words{
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
    EXPECT_EQ(computelab::ex2::EncodeWordInputLittleEndian(words),
        (std::vector<std::uint8_t>{
            0x9DU, 0xAAU, 0x8FU, 0xA4U,
            0x1AU, 0xCAU, 0x74U, 0xC3U,
            0xA2U, 0xCAU, 0xECU, 0x3BU,
            0xF8U, 0xCEU, 0x2DU, 0x91U}));
}

TEST(Ex2InputDigests, GeneratedBuffersMatchIndependentFixedDigests)
{
    const auto words = computelab::ex2::GenerateWordInput(CoreInputSeed, 4U);
    const auto bytes = computelab::ex2::GenerateByteInput(CoreInputSeed, 4U);
    const auto repeatedWords = computelab::ex2::GenerateWordInput(
        CoreInputSeed, 4U);
    const auto repeatedBytes = computelab::ex2::GenerateByteInput(
        CoreInputSeed, 4U);

    EXPECT_EQ(computelab::ex2::WordInputSha256(words),
        "e91b6862138c2e366b1976eaa23c1685054eb3b0636936dc4b572f3881585bc9");
    EXPECT_EQ(computelab::ex2::ByteInputSha256(bytes),
        "d09d1a6a9155c673b854b703ab7bed8b1eb5629a8b6309b041c84ec0643e8a21");
    EXPECT_EQ(computelab::ex2::WordInputSha256(words),
        computelab::ex2::WordInputSha256(repeatedWords));
    EXPECT_EQ(computelab::ex2::ByteInputSha256(bytes),
        computelab::ex2::ByteInputSha256(repeatedBytes));
}

TEST(Ex2InputDigests, EmptyAndChangedContentsMatchIndependentFixedDigests)
{
    const std::vector<std::uint8_t> empty;
    const std::vector<std::uint8_t> original{0x9DU, 0x1AU, 0xA2U, 0xF8U};
    const std::vector<std::uint8_t> changed{0x9DU, 0x1AU, 0xA2U, 0xF9U};

    EXPECT_EQ(computelab::ex2::ByteInputSha256(empty),
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(computelab::ex2::ByteInputSha256(original),
        "d09d1a6a9155c673b854b703ab7bed8b1eb5629a8b6309b041c84ec0643e8a21");
    EXPECT_EQ(computelab::ex2::ByteInputSha256(changed),
        "e253ea6070b26852307ad4c77080fa9c7013beafde6a464f0726d2ff17e2a780");
    EXPECT_NE(computelab::ex2::ByteInputSha256(original),
        computelab::ex2::ByteInputSha256(changed));
}

} // namespace

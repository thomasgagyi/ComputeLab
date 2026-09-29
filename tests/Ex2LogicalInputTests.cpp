#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2IndexPermutation.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2LogicalInput.hpp"
#include "ex2/Ex2Sha256.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace
{
namespace ex2 = computelab::ex2;

void AppendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    for (unsigned int shift : {0U, 8U, 16U, 24U})
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void AppendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value)
{
    for (unsigned int shift = 0U; shift < 64U; shift += 8U)
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
}

void AppendReferenceComponent(
    std::vector<std::uint8_t>& bytes,
    std::string_view name,
    std::span<const std::uint32_t> words)
{
    AppendU32(bytes, static_cast<std::uint32_t>(name.size()));
    bytes.insert(bytes.end(), name.begin(), name.end());
    AppendU64(bytes, static_cast<std::uint64_t>(words.size()) * 4U);
    for (const std::uint32_t word : words)
        AppendU32(bytes, word);
}

std::vector<std::uint8_t> ReferenceEncoding(
    std::string_view firstName,
    std::span<const std::uint32_t> first,
    std::string_view secondName,
    std::span<const std::uint32_t> second)
{
    std::vector<std::uint8_t> bytes;
    bytes.insert(bytes.end(), ex2::LogicalInputDomain.begin(),
        ex2::LogicalInputDomain.end());
    bytes.push_back(0U);
    AppendU32(bytes, 2U);
    AppendReferenceComponent(bytes, firstName, first);
    AppendReferenceComponent(bytes, secondName, second);
    return bytes;
}

std::string Digest(std::span<const std::uint8_t> bytes)
{
    return ex2::Sha256(std::as_bytes(bytes));
}

TEST(Ex2LogicalInput, BGoldenFixtureMatchesLiteralCanonicalByteStream)
{
    const std::array<std::uint32_t, 4> primary{
        0x12345678U, 0U, 0xFFFFFFFFU, 0xABCDEF01U};
    const std::array<std::uint32_t, 4> permutation{2U, 0U, 3U, 1U};
    const std::array<std::uint8_t, 123> literal{
        0x43U,0x6FU,0x6DU,0x70U,0x75U,0x74U,0x65U,0x4CU,
        0x61U,0x62U,0x2FU,0x45U,0x58U,0x2DU,0x32U,0x2FU,
        0x6CU,0x6FU,0x67U,0x69U,0x63U,0x61U,0x6CU,0x2DU,
        0x69U,0x6EU,0x70U,0x75U,0x74U,0x2FU,0x76U,0x31U,
        0x00U,0x02U,0x00U,0x00U,0x00U,
        0x0DU,0x00U,0x00U,0x00U,
        0x70U,0x72U,0x69U,0x6DU,0x61U,0x72U,0x79U,0x5FU,
        0x77U,0x6FU,0x72U,0x64U,0x73U,
        0x10U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
        0x78U,0x56U,0x34U,0x12U,0x00U,0x00U,0x00U,0x00U,
        0xFFU,0xFFU,0xFFU,0xFFU,0x01U,0xEFU,0xCDU,0xABU,
        0x11U,0x00U,0x00U,0x00U,
        0x70U,0x65U,0x72U,0x6DU,0x75U,0x74U,0x61U,0x74U,
        0x69U,0x6FU,0x6EU,0x5FU,0x77U,0x6FU,0x72U,0x64U,0x73U,
        0x10U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
        0x02U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,0x00U,
        0x03U,0x00U,0x00U,0x00U,0x01U,0x00U,0x00U,0x00U};

    const auto independentlyEncoded = ReferenceEncoding(
        "primary_words", primary, "permutation_words", permutation);
    EXPECT_EQ(independentlyEncoded,
        (std::vector<std::uint8_t>{literal.begin(), literal.end()}));
    EXPECT_EQ(Digest(literal),
        "c1b25198fd9f902e15f42b3d0080e893d4101f113855602720a63bf063a9e2de");
    EXPECT_EQ(ex2::IndexedLogicalInputSha256(primary, permutation),
        "c1b25198fd9f902e15f42b3d0080e893d4101f113855602720a63bf063a9e2de");
}

TEST(Ex2LogicalInput, CGoldenFixtureMatchesIndependentEncoding)
{
    const std::array<std::uint32_t, 4> targets{3U, 0U, 3U, 1U};
    const std::array<std::uint32_t, 4> counters{0U, 0U, 0U, 0U};
    const auto bytes = ReferenceEncoding(
        "targets_words", targets, "initial_counters_words", counters);

    ASSERT_EQ(bytes.size(), 128U);
    EXPECT_EQ(bytes[32], 0U);
    EXPECT_EQ((std::array<std::uint8_t, 4>{
        bytes[33], bytes[34], bytes[35], bytes[36]}),
        (std::array<std::uint8_t, 4>{2U, 0U, 0U, 0U}));
    EXPECT_EQ(Digest(bytes),
        "57cdcea826ef422677831a21be364ee2c44d203e533ac40c53b16f6d697fa9b7");
    EXPECT_EQ(ex2::ContentionLogicalInputSha256(targets, counters),
        "57cdcea826ef422677831a21be364ee2c44d203e533ac40c53b16f6d697fa9b7");
}

TEST(Ex2LogicalInput, FramingOrderNamesLengthsEndiannessAndContentsAreBound)
{
    const std::array<std::uint32_t, 4> primary{
        0x12345678U, 0U, 0xFFFFFFFFU, 0xABCDEF01U};
    const std::array<std::uint32_t, 4> permutation{2U, 0U, 3U, 1U};
    const auto canonical = ReferenceEncoding(
        "primary_words", primary, "permutation_words", permutation);
    const std::string digest = Digest(canonical);

    EXPECT_NE(Digest(ReferenceEncoding(
        "permutation_words", permutation, "primary_words", primary)), digest);
    EXPECT_NE(Digest(ReferenceEncoding(
        "primary_wordz", primary, "permutation_words", permutation)), digest);

    auto changedLength = canonical;
    changedLength[54] = 0x0CU;
    EXPECT_NE(Digest(changedLength), digest);
    auto bigEndianWord = canonical;
    std::reverse(bigEndianWord.begin() + 62, bigEndianWord.begin() + 66);
    EXPECT_NE(Digest(bigEndianWord), digest);

    auto changedPrimary = primary;
    changedPrimary[1] = 1U;
    EXPECT_NE(ex2::IndexedLogicalInputSha256(changedPrimary, permutation), digest);
    auto changedPermutation = permutation;
    std::swap(changedPermutation[0], changedPermutation[1]);
    EXPECT_NE(ex2::IndexedLogicalInputSha256(primary, changedPermutation), digest);
}

TEST(Ex2LogicalInput, EmptyOneWordAndAwkwardLengthsAreDeterministic)
{
    const std::vector<std::uint32_t> empty;
    const std::vector<std::uint32_t> one{0x12345678U};
    std::vector<std::uint32_t> awkward(1031U);
    for (std::size_t index = 0U; index < awkward.size(); ++index)
        awkward[index] = static_cast<std::uint32_t>(index * 17U);

    EXPECT_EQ(ex2::IndexedLogicalInputSha256(empty, empty),
        Digest(ReferenceEncoding(
            "primary_words", empty, "permutation_words", empty)));
    EXPECT_EQ(ex2::IndexedLogicalInputSha256(one, one),
        Digest(ReferenceEncoding(
            "primary_words", one, "permutation_words", one)));
    const auto copy = awkward;
    EXPECT_EQ(ex2::IndexedLogicalInputSha256(awkward, copy),
        ex2::IndexedLogicalInputSha256(copy, awkward));
}

TEST(Ex2LogicalInput, CheckedLengthsAndZeroStateRejectUnrepresentableRequests)
{
    EXPECT_EQ(ex2::LogicalInputWordPayloadByteLength(0U), 0U);
    EXPECT_EQ(ex2::LogicalInputWordPayloadByteLength(1U), 4U);
    EXPECT_EQ(ex2::LogicalInputWordPayloadByteLength(
        std::numeric_limits<std::uint64_t>::max() / 4U),
        (std::numeric_limits<std::uint64_t>::max() / 4U) * 4U);
    EXPECT_THROW(static_cast<void>(ex2::LogicalInputWordPayloadByteLength(
        std::numeric_limits<std::uint64_t>::max() / 4U + 1U)),
        std::length_error);
    EXPECT_THROW(static_cast<void>(ex2::MakeZeroInitialCounterState(
        std::numeric_limits<std::uint64_t>::max())), std::length_error);

    const auto zeros = ex2::MakeZeroInitialCounterState(257U);
    ASSERT_EQ(zeros.size(), 257U);
    EXPECT_TRUE(std::all_of(zeros.begin(), zeros.end(),
        [](std::uint32_t value) { return value == 0U; }));
}

TEST(Ex2LogicalInput, BothBComponentsAndBothCComponentsIndependentlyAffectIdentity)
{
    const auto primary = ex2::GenerateWordInput(ex2::CoreInputSeed, 257U);
    const auto structured = ex2::GenerateStructuredPermutation(257U);
    const auto shuffled = ex2::GenerateShuffledPermutation(
        ex2::CoreInputSeed, 257U);
    EXPECT_NE(structured, shuffled);
    EXPECT_NE(ex2::IndexedLogicalInputSha256(primary, structured),
        ex2::IndexedLogicalInputSha256(primary, shuffled));

    auto targets = ex2::GenerateContentionTargets(128U, 64U);
    std::vector<std::uint32_t> counters(128U, 0U);
    const std::string baseline =
        ex2::ContentionLogicalInputSha256(targets, counters);
    targets[0] = targets[0] == 0U ? 1U : 0U;
    EXPECT_NE(ex2::ContentionLogicalInputSha256(targets, counters), baseline);
    targets = ex2::GenerateContentionTargets(128U, 64U);
    counters.back() = 1U;
    EXPECT_NE(ex2::ContentionLogicalInputSha256(targets, counters), baseline);
}

TEST(Ex2LogicalInput, ApprovedGeneratedFixturesHashEveryDeclaredBuffer)
{
    const auto primary = ex2::GenerateWordInput(ex2::CoreInputSeed, 262'144U);
    const auto permutation = ex2::GenerateStructuredPermutation(262'144U);
    const std::string indexed =
        ex2::IndexedLogicalInputSha256(primary, permutation);
    EXPECT_NE(indexed, ex2::WordInputSha256(primary));
    EXPECT_NE(indexed, ex2::WordInputSha256(permutation));

    const auto targets = ex2::GenerateContentionTargets(
        ex2::ContentionElementCount, ex2::ContentionActive64);
    const auto initial = ex2::MakeZeroInitialCounterState(
        ex2::ContentionElementCount);
    const std::string contention =
        ex2::ContentionLogicalInputSha256(targets, initial);
    EXPECT_NE(contention, ex2::WordInputSha256(targets));
    EXPECT_NE(contention, ex2::WordInputSha256(initial));
}

} // namespace

#include "ex2/Ex2CpuOracles.hpp"

#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{

using computelab::ex2::TransferDirection;

TEST(Ex2CpuE, BothDirectionsUseTheExactGeneratedByteSequence)
{
    const std::vector<std::uint8_t> expected{0x9DU, 0x1AU, 0xA2U, 0xF8U};
    for (const auto direction
        : {TransferDirection::HostToDevice, TransferDirection::DeviceToHost})
    {
        const auto reference = computelab::ex2::ReferenceTransfer(
            computelab::ex2::CoreInputSeed, 4U, direction);
        EXPECT_EQ(reference.direction, direction);
        EXPECT_EQ(reference.source, expected);
        EXPECT_EQ(reference.expectedDestination, expected);
        EXPECT_TRUE(computelab::ex2::ValidateTransferOutput(
            reference, expected));
        EXPECT_EQ(computelab::ex2::ByteInputSha256(
            reference.expectedDestination),
            "d09d1a6a9155c673b854b703ab7bed8b1eb5629a8b6309b041c84ec0643e8a21");
    }
}

TEST(Ex2CpuE, ZeroByteCorrectnessCasesAreExplicitlyValid)
{
    for (const auto direction
        : {TransferDirection::HostToDevice, TransferDirection::DeviceToHost})
    {
        const auto reference = computelab::ex2::ReferenceTransfer(
            computelab::ex2::CoreInputSeed, 0U, direction);
        EXPECT_TRUE(reference.source.empty());
        EXPECT_TRUE(reference.expectedDestination.empty());
        EXPECT_TRUE(computelab::ex2::ValidateTransferOutput(reference, {}));
    }
}

TEST(Ex2CpuE, ValidationDetectsTruncationExtensionAndMismatchWithoutMutatingSource)
{
    auto reference = computelab::ex2::ReferenceTransfer(
        computelab::ex2::CoreInputSeed, 4U,
        TransferDirection::HostToDevice);
    const auto originalSource = reference.source;
    auto mismatch = reference.expectedDestination;
    mismatch[2] ^= 1U;
    auto extended = reference.expectedDestination;
    extended.push_back(0U);

    EXPECT_FALSE(computelab::ex2::ValidateTransferOutput(reference,
        std::span<const std::uint8_t>{
            reference.expectedDestination.data(),
            reference.expectedDestination.size() - 1U}));
    EXPECT_FALSE(computelab::ex2::ValidateTransferOutput(reference, mismatch));
    EXPECT_FALSE(computelab::ex2::ValidateTransferOutput(reference, extended));
    EXPECT_EQ(reference.source, originalSource);
}

TEST(Ex2CpuE, RepeatedCallsAndDirectionOrderDoNotChangeByteIdentity)
{
    const auto deviceToHost = computelab::ex2::ReferenceTransfer(
        computelab::ex2::CoreInputSeed, 257U,
        TransferDirection::DeviceToHost);
    const auto hostToDevice = computelab::ex2::ReferenceTransfer(
        computelab::ex2::CoreInputSeed, 257U,
        TransferDirection::HostToDevice);
    const auto repeated = computelab::ex2::ReferenceTransfer(
        computelab::ex2::CoreInputSeed, 257U,
        TransferDirection::DeviceToHost);
    EXPECT_EQ(deviceToHost.source, hostToDevice.source);
    EXPECT_EQ(deviceToHost.source, repeated.source);
}

TEST(Ex2CpuE, RejectsInvalidDirectionAndExcessiveByteCount)
{
    EXPECT_THROW(static_cast<void>(computelab::ex2::ReferenceTransfer(
        computelab::ex2::CoreInputSeed, 1U,
        static_cast<TransferDirection>(99))), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(computelab::ex2::ReferenceTransfer(
        computelab::ex2::CoreInputSeed,
        std::numeric_limits<std::uint64_t>::max(),
        TransferDirection::HostToDevice)), std::length_error);
}

} // namespace

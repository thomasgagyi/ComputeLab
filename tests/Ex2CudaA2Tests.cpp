#include "cuda/Ex2CudaA2.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <cuda_runtime_api.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace
{

namespace cuda = computelab::cuda;
namespace ex2 = computelab::ex2;

ex2::LinearConfiguration A2Configuration(std::uint64_t elementCount)
{
    return {ex2::LinearVariant::A2, elementCount};
}

std::vector<std::uint32_t> Execute(
    cuda::Ex2CudaA2Operation& operation,
    const std::vector<std::uint32_t>& input)
{
    operation.Upload(input);
    operation.SubmitA2();
    operation.WaitForCompletion();
    return operation.RetrieveOutput();
}

TEST(Ex2CudaA2, ExactRequiredSizesMatchCpuOracleAndPreserveInput)
{
    constexpr std::array<std::uint64_t, 8> sizes{
        0U, 1U, 4U, 255U, 256U, 257U, 4'097U, 262'144U};

    for (const std::uint64_t size : sizes)
    {
        SCOPED_TRACE(size);
        const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, size);
        const auto expected = ex2::ReferenceA2(input);
        cuda::Ex2CudaA2Operation operation{0, A2Configuration(size)};
        const auto actual = Execute(operation, input);

        EXPECT_EQ(actual.size(), size);
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(operation.RetrieveDeviceInput(), input);
        EXPECT_EQ(operation.LastCompletionExecutedKernel(), size != 0U);
    }
}

TEST(Ex2CudaA2, LiteralFixtureIndependentlyAnchorsOracleAndGpu)
{
    const std::vector<std::uint32_t> input{
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
    const std::vector<std::uint32_t> expected{
        0x4579A7C6U, 0x6D06CDCFU, 0x3BE3E55EU, 0x271DAF20U};

    EXPECT_EQ(ex2::ReferenceA2(input), expected);
    cuda::Ex2CudaA2Operation operation{0, A2Configuration(input.size())};
    EXPECT_EQ(Execute(operation, input), expected);
}

TEST(Ex2CudaA2, ZeroElementsUseNoKernelLifecycle)
{
    cuda::Ex2CudaA2Operation operation{0, A2Configuration(0U)};
    operation.Upload({});
    operation.SubmitA2();
    operation.WaitForCompletion();
    EXPECT_TRUE(operation.RetrieveOutput().empty());
    EXPECT_TRUE(operation.RetrieveDeviceInput().empty());
    EXPECT_FALSE(operation.LastCompletionExecutedKernel());
}

TEST(Ex2CudaA2, RepeatedOperationsAreDeterministicAndDoNotReturnStaleOutput)
{
    const auto firstInput = ex2::GenerateWordInput(ex2::CoreInputSeed, 257U);
    const auto secondInput = ex2::GenerateWordInput(
        0xFEDCBA9876543210ULL, 257U);
    cuda::Ex2CudaA2Operation operation{0, A2Configuration(firstInput.size())};

    const auto first = Execute(operation, firstInput);
    const auto repeated = Execute(operation, firstInput);
    const auto second = Execute(operation, secondInput);

    EXPECT_EQ(first, ex2::ReferenceA2(firstInput));
    EXPECT_EQ(repeated, first);
    EXPECT_EQ(second, ex2::ReferenceA2(secondInput));
    EXPECT_NE(second, first);
    EXPECT_EQ(operation.RetrieveDeviceInput(), secondInput);
}

TEST(Ex2CudaA2, InvalidLifecycleAndUploadLengthFailExplicitly)
{
    const std::vector<std::uint32_t> input{0x12345678U};
    cuda::Ex2CudaA2Operation operation{0, A2Configuration(input.size())};
    EXPECT_THROW(operation.SubmitA2(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveOutput()), std::logic_error);
    EXPECT_THROW(operation.Upload({}), std::invalid_argument);
    operation.Upload(input);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    operation.SubmitA2();
    EXPECT_THROW(operation.SubmitA2(), std::logic_error);
    EXPECT_THROW(operation.Upload(input), std::logic_error);
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveOutput(), ex2::ReferenceA2(input));
}

TEST(Ex2CudaA2Guards, RejectsA1OversizedAndInfeasibleConfigurations)
{
    EXPECT_THROW(
        static_cast<void>(cuda::Ex2CudaA2Operation{
            0, {ex2::LinearVariant::A1, 1U}}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaA2LaunchShape(
            {ex2::LinearVariant::A1, 1U}, 1U, 256U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaA2LaunchShape(
            A2Configuration(
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max()) + 1U),
            std::numeric_limits<std::uint32_t>::max(), 256U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaA2LaunchShape(
            A2Configuration(257U), 1U, 256U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::CalculateEx2CudaA2BufferByteCount(
            257U, 1'024U)),
        std::length_error);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaA2MemoryFeasibility(
        513U, 1'024U, 2'048U), std::length_error);
}

TEST(Ex2CudaA2Guards, CalculatesExactAndPaddedLaunchesWithoutAllocation)
{
    const auto zero = cuda::detail::ValidateEx2CudaA2LaunchShape(
        A2Configuration(0U), 1U, 1U, 1U);
    const auto exact = cuda::detail::ValidateEx2CudaA2LaunchShape(
        A2Configuration(256U), 1U, 256U, 256U);
    const auto padded = cuda::detail::ValidateEx2CudaA2LaunchShape(
        A2Configuration(257U), 2U, 256U, 256U);
    EXPECT_EQ(zero.blockCount, 0U);
    EXPECT_EQ(exact.blockCount, 1U);
    EXPECT_EQ(padded.blockCount, 2U);
    EXPECT_EQ(padded.threadsPerBlock, 256U);
}

TEST(Ex2CudaA2, DeviceIdentityAndNativeSelectionFailureRemainExplicit)
{
    cudaDeviceProp properties{};
    ASSERT_EQ(cudaGetDeviceProperties(&properties, 0), cudaSuccess);
    cuda::Ex2CudaA2Operation operation{0, A2Configuration(1U)};
    EXPECT_EQ(operation.SelectedDeviceOrdinal(), 0);
    for (std::size_t index = 0U; index < operation.SelectedDeviceUuid().size(); ++index)
    {
        EXPECT_EQ(operation.SelectedDeviceUuid()[index],
            static_cast<std::uint8_t>(properties.uuid.bytes[index]));
    }

    try
    {
        cuda::Ex2CudaA2Operation invalid{-1, A2Configuration(1U)};
        FAIL() << "expected invalid CUDA device selection to fail";
    }
    catch (const cuda::Ex2CudaA2NativeError& error)
    {
        EXPECT_EQ(error.Phase(), cuda::Ex2CudaA2NativePhase::DeviceSelection);
        EXPECT_NE(error.NativeErrorCode(), 0);
    }
}

TEST(Ex2CudaA2, UncertainCompletionRequiresProcessTeardownDisposition)
{
    EXPECT_EQ(
        cuda::detail::ClassifyEx2CudaA2ResourceDisposition(true),
        cuda::detail::Ex2CudaA2ResourceDisposition::DestroyNormally);
    EXPECT_EQ(
        cuda::detail::ClassifyEx2CudaA2ResourceDisposition(false),
        cuda::detail::Ex2CudaA2ResourceDisposition::PreserveForProcessTeardown);
}

} // namespace

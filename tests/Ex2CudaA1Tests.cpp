#include "cuda/Ex2CudaA1.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <cuda_runtime_api.h>

#include <array>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

namespace cuda = computelab::cuda;
namespace ex2 = computelab::ex2;

ex2::LinearConfiguration A1Configuration(std::uint64_t elementCount)
{
    return {ex2::LinearVariant::A1, elementCount};
}

std::vector<std::uint32_t> Execute(
    cuda::Ex2CudaA1Operation& operation,
    const std::vector<std::uint32_t>& input)
{
    operation.Upload(input);
    operation.SubmitA1();
    operation.WaitForCompletion();
    return operation.RetrieveOutput();
}

TEST(Ex2CudaA1, ExactBoundedSizesMatchIndependentCpuOracleAndPreserveInput)
{
    constexpr std::array<std::uint64_t, 7> sizes{
        0U, 1U, 4U, 255U, 256U, 257U, 262'144U};

    for (const std::uint64_t size : sizes)
    {
        SCOPED_TRACE(size);
        const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, size);
        const auto expected = ex2::ReferenceA1(input);
        cuda::Ex2CudaA1Operation operation{0, A1Configuration(size)};

        const auto actual = Execute(operation, input);

        EXPECT_EQ(actual.size(), size);
        EXPECT_EQ(actual, expected);
        EXPECT_EQ(operation.RetrieveDeviceInput(), input);
        EXPECT_EQ(operation.LastCompletionExecutedKernel(), size != 0U);
    }
}

TEST(Ex2CudaA1, LiteralFourWordFixtureIndependentlyAnchorsOracleAndGpu)
{
    const std::vector<std::uint32_t> input{
        0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
    const std::vector<std::uint32_t> expected{
        0x3AB8D324U, 0x5D43B3A0U, 0xA5DBB319U, 0x0F1AB744U};

    EXPECT_EQ(ex2::ReferenceA1(input), expected);

    cuda::Ex2CudaA1Operation operation{0, A1Configuration(input.size())};
    EXPECT_EQ(Execute(operation, input), expected);
    EXPECT_TRUE(operation.LastCompletionExecutedKernel());
}

TEST(Ex2CudaA1, ZeroElementsUseDefinedNoWorkLifecycleWithoutKernelDispatch)
{
    cuda::Ex2CudaA1Operation operation{0, A1Configuration(0U)};
    operation.Upload({});
    operation.SubmitA1();
    operation.WaitForCompletion();

    EXPECT_TRUE(operation.RetrieveOutput().empty());
    EXPECT_TRUE(operation.RetrieveDeviceInput().empty());
    EXPECT_FALSE(operation.LastCompletionExecutedKernel());
}

TEST(Ex2CudaA1, RepeatedIdenticalOperationsAreDeterministic)
{
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, 4'097U);
    const auto expected = ex2::ReferenceA1(input);
    cuda::Ex2CudaA1Operation operation{0, A1Configuration(input.size())};

    const auto first = Execute(operation, input);
    const auto second = Execute(operation, input);

    EXPECT_EQ(first, expected);
    EXPECT_EQ(second, expected);
    EXPECT_EQ(first, second);
}

TEST(Ex2CudaA1, RepeatedDifferentUploadsCannotReturnStaleResults)
{
    const auto firstInput = ex2::GenerateWordInput(ex2::CoreInputSeed, 257U);
    const auto secondInput = ex2::GenerateWordInput(0xFEDCBA9876543210ULL, 257U);
    ASSERT_NE(firstInput, secondInput);
    cuda::Ex2CudaA1Operation operation{0, A1Configuration(firstInput.size())};

    const auto first = Execute(operation, firstInput);
    const auto second = Execute(operation, secondInput);

    EXPECT_EQ(first, ex2::ReferenceA1(firstInput));
    EXPECT_EQ(second, ex2::ReferenceA1(secondInput));
    EXPECT_NE(first, second);
    EXPECT_EQ(operation.RetrieveDeviceInput(), secondInput);
}

TEST(Ex2CudaA1, InvalidLifecycleTransitionsFailExplicitly)
{
    const std::vector<std::uint32_t> input{0x12345678U};
    cuda::Ex2CudaA1Operation operation{0, A1Configuration(input.size())};

    EXPECT_THROW(operation.SubmitA1(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveOutput()),
        std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDeviceInput()),
        std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.LastCompletionExecutedKernel()),
        std::logic_error);

    operation.Upload(input);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    operation.SubmitA1();
    EXPECT_THROW(operation.SubmitA1(), std::logic_error);
    EXPECT_THROW(operation.Upload(input), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveOutput()),
        std::logic_error);
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveOutput(), ex2::ReferenceA1(input));
    EXPECT_THROW(operation.SubmitA1(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
}

TEST(Ex2CudaA1, UploadRequiresExactConfiguredLength)
{
    cuda::Ex2CudaA1Operation operation{0, A1Configuration(1U)};
    EXPECT_THROW(operation.Upload({}), std::invalid_argument);

    const std::vector<std::uint32_t> tooLarge{1U, 2U};
    EXPECT_THROW(operation.Upload(tooLarge), std::invalid_argument);
}

TEST(Ex2CudaA1Guards, RejectsNonA1AndOversizedSemanticConfigurations)
{
    EXPECT_THROW(
        static_cast<void>(cuda::Ex2CudaA1Operation{
            0, {ex2::LinearVariant::A2, 1U}}),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaA1LaunchShape(
            {ex2::LinearVariant::A2, 1U}, 1U, 256U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaA1LaunchShape(
            A1Configuration(
                static_cast<std::uint64_t>(
                    std::numeric_limits<std::uint32_t>::max()) + 1U),
            std::numeric_limits<std::uint32_t>::max(),
            256U,
            256U)),
        std::invalid_argument);
}

TEST(Ex2CudaA1Guards, CalculatesBoundaryLaunchesAndRejectsInfeasibleDispatch)
{
    const auto zero = cuda::detail::ValidateEx2CudaA1LaunchShape(
        A1Configuration(0U), 1U, 1U, 1U);
    EXPECT_EQ(zero.blockCount, 0U);

    const auto exact = cuda::detail::ValidateEx2CudaA1LaunchShape(
        A1Configuration(256U), 1U, 256U, 256U);
    EXPECT_EQ(exact.blockCount, 1U);
    EXPECT_EQ(exact.threadsPerBlock, 256U);

    const auto padded = cuda::detail::ValidateEx2CudaA1LaunchShape(
        A1Configuration(257U), 2U, 256U, 256U);
    EXPECT_EQ(padded.blockCount, 2U);

    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaA1LaunchShape(
            A1Configuration(257U), 1U, 256U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaA1LaunchShape(
            A1Configuration(1U), 1U, 255U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::ValidateEx2CudaA1LaunchShape(
            A1Configuration(1U), 1U, 256U, 255U)),
        std::invalid_argument);
}

TEST(Ex2CudaA1Guards, RejectsByteOverflowWithoutAllocation)
{
    EXPECT_EQ(
        cuda::detail::CalculateEx2CudaA1BufferByteCount(256U, 1'024U),
        1'024U);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::CalculateEx2CudaA1BufferByteCount(
            257U, 1'024U)),
        std::length_error);
    EXPECT_THROW(
        static_cast<void>(cuda::detail::CalculateEx2CudaA1BufferByteCount(
            static_cast<std::uint64_t>(
                std::numeric_limits<std::uint32_t>::max()) + 1U,
            std::numeric_limits<std::size_t>::max())),
        std::invalid_argument);
}

TEST(Ex2CudaA1Guards, RejectsKnownInfeasibleMemoryWithoutAllocation)
{
    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaA1MemoryFeasibility(
        0U, 0U, 0U));
    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaA1MemoryFeasibility(
        512U, 1'024U, 1'024U));
    EXPECT_THROW(cuda::detail::ValidateEx2CudaA1MemoryFeasibility(
        513U, 1'024U, 2'048U), std::length_error);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaA1MemoryFeasibility(
        513U, 2'048U, 1'024U), std::length_error);
}

TEST(Ex2CudaA1, SelectedOrdinalAndUuidComeFromTheCudaRuntime)
{
    cudaDeviceProp properties{};
    ASSERT_EQ(cudaGetDeviceProperties(&properties, 0), cudaSuccess);
    cuda::Ex2CudaA1Operation operation{0, A1Configuration(1U)};

    EXPECT_EQ(operation.SelectedDeviceOrdinal(), 0);
    const auto& uuid = operation.SelectedDeviceUuid();
    for (std::size_t index = 0U; index < uuid.size(); ++index)
    {
        EXPECT_EQ(
            uuid[index],
            static_cast<std::uint8_t>(properties.uuid.bytes[index]));
    }
}

TEST(Ex2CudaA1, InvalidDeviceSelectionPropagatesNativeCodeAndPhase)
{
    try
    {
        cuda::Ex2CudaA1Operation operation{-1, A1Configuration(1U)};
        FAIL() << "expected invalid CUDA device selection to fail";
    }
    catch (const cuda::Ex2CudaA1NativeError& error)
    {
        EXPECT_EQ(
            error.Phase(),
            cuda::Ex2CudaA1NativePhase::DeviceSelection);
        EXPECT_NE(error.NativeErrorCode(), 0);
        EXPECT_FALSE(error.NativeErrorName().empty());
        EXPECT_NE(
            std::string{error.what()}.find("device selection"),
            std::string::npos);
    }
}

TEST(Ex2CudaA1, OnlyEstablishedCompletionAllowsNormalResourceDestruction)
{
    using cuda::detail::ClassifyEx2CudaA1ResourceDisposition;
    using cuda::detail::Ex2CudaA1ResourceDisposition;

    EXPECT_EQ(
        ClassifyEx2CudaA1ResourceDisposition(true),
        Ex2CudaA1ResourceDisposition::DestroyNormally);
    EXPECT_EQ(
        ClassifyEx2CudaA1ResourceDisposition(false),
        Ex2CudaA1ResourceDisposition::PreserveForProcessTeardown);
}

} // namespace

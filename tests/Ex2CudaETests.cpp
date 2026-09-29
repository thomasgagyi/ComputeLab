#include "cuda/Ex2CudaE.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <cuda_runtime.h>
#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

namespace
{

namespace cuda = computelab::cuda;
namespace ex2 = computelab::ex2;

constexpr std::array<std::uint8_t, 4U> LiteralBytes{
    0x9DU, 0x1AU, 0xA2U, 0xF8U};

int RequireCudaDevice()
{
    int count{};
    EXPECT_EQ(cudaGetDeviceCount(&count), cudaSuccess);
    EXPECT_GT(count, 0);
    return 0;
}

ex2::TransferConfiguration Configuration(
    ex2::TransferDirection direction,
    std::uint64_t byteCount)
{
    return {
        direction == ex2::TransferDirection::HostToDevice
            ? ex2::TransferVariant::E1
            : ex2::TransferVariant::E2,
        byteCount,
        direction};
}

void RunCore(ex2::TransferDirection direction, std::uint64_t byteCount)
{
    auto reference = ex2::ReferenceTransfer(
        ex2::CoreInputSeed, byteCount, direction);
    cuda::Ex2CudaEOperation operation{
        RequireCudaDevice(), Configuration(direction, byteCount)};
    EXPECT_EQ(operation.ByteCount(), byteCount);
    EXPECT_EQ(operation.Direction(), direction);
    EXPECT_EQ(operation.SelectedDeviceUuid().size(), 16U);
    EXPECT_EQ(operation.ExpectedNativeCopyCount(), 1U);

    operation.PrepareTransfer(reference.source);
    operation.SubmitTransfer();
    operation.WaitForCompletion();
    const auto first = operation.RetrieveDestinationForValidation();
    EXPECT_EQ(first.size(), reference.source.size());
    EXPECT_TRUE(ex2::ValidateTransferOutput(reference, first));
    EXPECT_EQ(reference.source, reference.expectedDestination);
    EXPECT_TRUE(operation.LastCompletionExecutedCopy());
    EXPECT_EQ(operation.LastCompletionNativeCopyCount(), 1U);

    operation.PrepareTransfer(reference.source);
    operation.SubmitTransfer();
    operation.WaitForCompletion();
    const auto second = operation.RetrieveDestinationForValidation();
    EXPECT_TRUE(ex2::ValidateTransferOutput(reference, second));
    EXPECT_EQ(second, first);
    EXPECT_EQ(reference.source, reference.expectedDestination);
    EXPECT_TRUE(operation.LastCompletionExecutedCopy());
    EXPECT_EQ(operation.LastCompletionNativeCopyCount(), 1U);
}

void RunCorrectnessOnly(
    ex2::TransferDirection direction,
    std::uint64_t byteCount)
{
    auto reference = ex2::ReferenceTransfer(
        ex2::CoreInputSeed, byteCount, direction);
    cuda::Ex2CudaEOperation operation{
        RequireCudaDevice(), Configuration(direction, byteCount)};
    operation.PrepareTransfer(reference.source);
    operation.SubmitTransfer();
    operation.WaitForCompletion();
    EXPECT_TRUE(ex2::ValidateTransferOutput(
        reference, operation.RetrieveDestinationForValidation()));
    EXPECT_EQ(reference.source, reference.expectedDestination);
    EXPECT_EQ(operation.LastCompletionNativeCopyCount(),
        byteCount == 0U ? 0U : 1U);
    EXPECT_EQ(operation.LastCompletionExecutedCopy(), byteCount != 0U);
}

} // namespace

TEST(Ex2CudaECore, E1S1024MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::HostToDevice, 1'024U);
}

TEST(Ex2CudaECore, E1S1048576MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::HostToDevice, 1'048'576U);
}

TEST(Ex2CudaECore, E1S67108864MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::HostToDevice, 67'108'864U);
}

TEST(Ex2CudaECore, E2S1024MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::DeviceToHost, 1'024U);
}

TEST(Ex2CudaECore, E2S1048576MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::DeviceToHost, 1'048'576U);
}

TEST(Ex2CudaECore, E2S67108864MatchesCompleteCpuReferenceAndRepeats)
{
    RunCore(ex2::TransferDirection::DeviceToHost, 67'108'864U);
}

TEST(Ex2CudaECorrectnessOnly, LiteralBytesAreIndependentForBothDirections)
{
    for (const auto direction : {
            ex2::TransferDirection::HostToDevice,
            ex2::TransferDirection::DeviceToHost})
    {
        cuda::Ex2CudaEOperation operation{
            RequireCudaDevice(), Configuration(direction, LiteralBytes.size())};
        operation.PrepareTransfer(LiteralBytes);
        operation.SubmitTransfer();
        operation.WaitForCompletion();
        EXPECT_EQ(
            operation.RetrieveDestinationForValidation(),
            std::vector<std::uint8_t>(LiteralBytes.begin(), LiteralBytes.end()));
    }
}

TEST(Ex2CudaECorrectnessOnly, ZeroOneAndOdd257AreByteExact)
{
    for (const auto direction : {
            ex2::TransferDirection::HostToDevice,
            ex2::TransferDirection::DeviceToHost})
    {
        for (const std::uint64_t byteCount : {0U, 1U, 257U})
            RunCorrectnessOnly(direction, byteCount);
    }
}

TEST(Ex2CudaE, SameObjectRepeatUsesReplacementSource)
{
    constexpr std::uint64_t byteCount = 257U;
    for (const auto direction : {
            ex2::TransferDirection::HostToDevice,
            ex2::TransferDirection::DeviceToHost})
    {
        auto first = ex2::GenerateByteInput(ex2::CoreInputSeed, byteCount);
        auto second = first;
        for (auto& value : second)
            value ^= 0x5AU;
        cuda::Ex2CudaEOperation operation{
            RequireCudaDevice(), Configuration(direction, byteCount)};
        operation.PrepareTransfer(first);
        operation.SubmitTransfer();
        operation.WaitForCompletion();
        EXPECT_EQ(operation.RetrieveDestinationForValidation(), first);
        operation.PrepareTransfer(second);
        operation.SubmitTransfer();
        operation.WaitForCompletion();
        EXPECT_EQ(operation.RetrieveDestinationForValidation(), second);
        EXPECT_NE(first, second);
    }
}

TEST(Ex2CudaE, LifecycleRejectsOutOfOrderAndDuplicateSubmission)
{
    const auto input = ex2::GenerateByteInput(ex2::CoreInputSeed, 4U);
    cuda::Ex2CudaEOperation operation{
        RequireCudaDevice(),
        Configuration(ex2::TransferDirection::HostToDevice, input.size())};
    EXPECT_THROW(operation.SubmitTransfer(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDestinationForValidation()),
        std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.LastCompletionExecutedCopy()),
        std::logic_error);
    operation.PrepareTransfer(input);
    operation.SubmitTransfer();
    EXPECT_THROW(operation.SubmitTransfer(), std::logic_error);
    EXPECT_THROW(
        static_cast<void>(operation.RetrieveDestinationForValidation()),
        std::logic_error);
    operation.WaitForCompletion();
    EXPECT_THROW(operation.SubmitTransfer(), std::logic_error);
}

TEST(Ex2CudaE, FailedReplacementPreparationInvalidatesPriorReadiness)
{
    const auto input = ex2::GenerateByteInput(ex2::CoreInputSeed, 4U);
    cuda::Ex2CudaEOperation operation{
        RequireCudaDevice(),
        Configuration(ex2::TransferDirection::DeviceToHost, input.size())};
    operation.PrepareTransfer(input);
    const std::array<std::uint8_t, 3U> wrongLength{};
    EXPECT_THROW(operation.PrepareTransfer(wrongLength), std::invalid_argument);
    EXPECT_THROW(operation.SubmitTransfer(), std::logic_error);
    EXPECT_THROW(operation.PrepareTransfer(input), std::logic_error);
}

TEST(Ex2CudaE, StaleRuntimeErrorDoesNotContaminateNamedCopyAttribution)
{
    const auto input = ex2::GenerateByteInput(ex2::CoreInputSeed, 257U);
    cuda::Ex2CudaEOperation operation{
        RequireCudaDevice(),
        Configuration(ex2::TransferDirection::HostToDevice, input.size())};
    operation.PrepareTransfer(input);
    const cudaError_t injectedError = cudaMemcpyAsync(
        nullptr,
        nullptr,
        1U,
        cudaMemcpyDeviceToDevice,
        nullptr);
    ASSERT_NE(injectedError, cudaSuccess);
    EXPECT_EQ(cudaPeekAtLastError(), injectedError);
    operation.SubmitTransfer();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveDestinationForValidation(), input);
}

TEST(Ex2CudaE, TimedE1AndE2MarkOnlyTheNamedCopy)
{
    for (const auto direction : {
            ex2::TransferDirection::HostToDevice,
            ex2::TransferDirection::DeviceToHost})
    {
        auto reference = ex2::ReferenceTransfer(
            ex2::CoreInputSeed, 257U, direction);
        cuda::Ex2CudaEDeviceTimedOperation operation{
            RequireCudaDevice(), Configuration(direction, 257U)};
        operation.PrepareTransfer(reference.source);
        operation.SubmitTransfer();
        operation.WaitForCompletion();
        operation.RetrieveNativeTiming();
        EXPECT_TRUE(ex2::ValidateTransferOutput(
            reference, operation.RetrieveDestinationForValidation()));
        EXPECT_EQ(operation.NativeTimingStatus(),
            cuda::Ex2CudaENativeTimingStatus::Valid);
        EXPECT_TRUE(operation.NativeDeviceIntervalNanoseconds().has_value());
        EXPECT_EQ(operation.NativeTimingMetadata().method,
            "cudaEventElapsedTime");
        EXPECT_EQ(operation.LastCompletionNativeCopyCount(), 1U);
    }
}

TEST(Ex2CudaE, TimedZeroByteTransferIsNotApplicable)
{
    cuda::Ex2CudaEDeviceTimedOperation operation{
        RequireCudaDevice(),
        Configuration(ex2::TransferDirection::HostToDevice, 0U)};
    operation.PrepareTransfer({});
    operation.SubmitTransfer();
    operation.WaitForCompletion();
    operation.RetrieveNativeTiming();
    EXPECT_EQ(operation.NativeTimingStatus(),
        cuda::Ex2CudaENativeTimingStatus::NotApplicable);
    EXPECT_FALSE(operation.NativeDeviceIntervalNanoseconds().has_value());
    EXPECT_FALSE(operation.LastCompletionExecutedCopy());
}

TEST(Ex2CudaEPure, ConfigurationResourceAndFailureSeams)
{
    static_assert(!cuda::Ex2CudaEOperation::EnqueuesDeviceTimestamps);
    static_assert(cuda::Ex2CudaEDeviceTimedOperation::EnqueuesDeviceTimestamps);
    auto valid = ex2::MakeConfiguration(Configuration(
        ex2::TransferDirection::HostToDevice, 257U));
    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaEConfiguration(valid));
    valid.common.executionMode = ex2::LogicalExecutionMode::Ordinary;
    EXPECT_THROW(cuda::detail::ValidateEx2CudaEConfiguration(valid),
        std::invalid_argument);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaEConfiguration(
        ex2::MakeConfiguration(ex2::TransferConfiguration{
            ex2::TransferVariant::E1,
            257U,
            ex2::TransferDirection::DeviceToHost})), std::invalid_argument);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaEConfiguration(
        ex2::MakeConfiguration(ex2::TransferConfiguration{
            static_cast<ex2::TransferVariant>(-1),
            257U,
            ex2::TransferDirection::HostToDevice})), std::invalid_argument);

    EXPECT_EQ(cuda::detail::CalculateEx2CudaEByteCount(257U, 257U), 257U);
    EXPECT_THROW(static_cast<void>(
        cuda::detail::CalculateEx2CudaEByteCount(258U, 257U)),
        std::length_error);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaEHostVectorCapacity(258U, 257U),
        std::length_error);
    const auto resources = cuda::detail::CalculateEx2CudaEResourceBytes(
        257U, 1'024U);
    EXPECT_EQ(resources.deviceBytes, 257U);
    EXPECT_EQ(resources.pinnedHostBytes, 514U);
    EXPECT_EQ(resources.totalBytes, 771U);
    EXPECT_THROW(static_cast<void>(cuda::detail::CalculateEx2CudaEResourceBytes(
        342U, 1'024U)), std::length_error);
    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaEDeviceMemoryFeasibility(
        4U, 4U, 4U));
    EXPECT_THROW(cuda::detail::ValidateEx2CudaEDeviceMemoryFeasibility(
        5U, 4U, 4U), std::length_error);

    using State = cuda::detail::Ex2CudaEOperationState;
    EXPECT_EQ(cuda::detail::ClassifyEx2CudaEAsyncFailure(false, false),
        State::Failed);
    EXPECT_EQ(cuda::detail::ClassifyEx2CudaEAsyncFailure(true, false),
        State::CompletionUncertain);
    const auto retained = cuda::detail::ClassifyEx2CudaECompletionDisposition(
        true, true);
    EXPECT_EQ(retained.deviceResources,
        cuda::detail::Ex2CudaEResourceDisposition::PreserveForProcessTeardown);
    EXPECT_EQ(retained.hostStorage,
        cuda::detail::Ex2CudaEHostStorageDisposition::PreserveForProcessTeardown);
    EXPECT_FALSE(retained.operationReusable);
    EXPECT_EQ(cuda::detail::Ex2CudaEMillisecondsToNanoseconds(0.0005F), 500U);
    EXPECT_THROW(static_cast<void>(
        cuda::detail::Ex2CudaEMillisecondsToNanoseconds(-1.0F)),
        std::invalid_argument);
}

TEST(Ex2CudaE, InvalidDeviceIsRejected)
{
    EXPECT_THROW(cuda::Ex2CudaEOperation(
        std::numeric_limits<int>::max(),
        Configuration(ex2::TransferDirection::HostToDevice, 1U)),
        cuda::Ex2CudaENativeError);
}

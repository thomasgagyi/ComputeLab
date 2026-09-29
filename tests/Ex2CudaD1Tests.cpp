#include "cuda/Ex2CudaD1.hpp"

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

constexpr std::array<std::uint32_t, 4U> InitialFixture{
    0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
constexpr std::array<std::uint32_t, 4U> KOneFixture{
    0xD664E09CU, 0x27C0F020U, 0x3AC0DF49U, 0x62A16496U};
constexpr std::array<std::uint32_t, 4U> KTwoFixture{
    0x89BDA0DEU, 0xBE3BAF8CU, 0x1E3F5AC9U, 0x120E2194U};

int RequireCudaDevice()
{
    int count{};
    EXPECT_EQ(cudaGetDeviceCount(&count), cudaSuccess);
    EXPECT_GT(count, 0);
    return 0;
}

std::vector<std::uint32_t> AsVector(
    const std::array<std::uint32_t, 4U>& value)
{
    return {value.begin(), value.end()};
}

void RunLiteral(std::uint64_t iterations, std::span<const std::uint32_t> expected)
{
    cuda::Ex2CudaD1Operation operation{
        RequireCudaDevice(),
        {ex2::IterativeVariant::D1, InitialFixture.size(), iterations}};
    operation.UploadInitialState(InitialFixture);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveFinalState(),
        std::vector<std::uint32_t>(expected.begin(), expected.end()));
    EXPECT_EQ(operation.CompletedFinalBuffer(),
        (iterations & 1U) == 0U
            ? ex2::IterativeFinalBuffer::StateA
            : ex2::IterativeFinalBuffer::StateB);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), iterations);
    EXPECT_TRUE(operation.LastCompletionExecutedSequence());
}

void RunCore(std::uint64_t elementCount, std::uint64_t iterationCount)
{
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, elementCount);
    const auto reference = ex2::ReferenceD1(input, iterationCount);
    cuda::Ex2CudaD1Operation operation{
        RequireCudaDevice(),
        {ex2::IterativeVariant::D1, elementCount, iterationCount}};
    EXPECT_EQ(operation.ElementCount(), elementCount);
    EXPECT_EQ(operation.IterationCount(), iterationCount);
    EXPECT_EQ(operation.ExpectedFinalBuffer(), reference.finalBuffer);

    operation.UploadInitialState(input);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    const auto first = operation.RetrieveFinalState();
    EXPECT_EQ(first.size(), input.size());
    EXPECT_EQ(first, reference.finalState);
    EXPECT_EQ(operation.CompletedFinalBuffer(), reference.finalBuffer);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), iterationCount);
    EXPECT_TRUE(operation.LastCompletionExecutedSequence());

    operation.UploadInitialState(input);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    const auto second = operation.RetrieveFinalState();
    EXPECT_EQ(second, reference.finalState);
    EXPECT_EQ(second, first);
    EXPECT_EQ(operation.CompletedFinalBuffer(), reference.finalBuffer);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), iterationCount);
    EXPECT_TRUE(operation.LastCompletionExecutedSequence());
}

} // namespace

TEST(Ex2CudaD1, LiteralKOneUsesStateB)
{
    RunLiteral(1U, KOneFixture);
}

TEST(Ex2CudaD1, LiteralKTwoUsesStateA)
{
    RunLiteral(2U, KTwoFixture);
}

TEST(Ex2CudaD1, PaddedN257MatchesIndependentCpuReference)
{
    constexpr std::uint64_t count = 257U;
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, count);
    const auto expected = ex2::ReferenceD1(input, 1U);
    cuda::Ex2CudaD1Operation operation{
        RequireCudaDevice(),
        {ex2::IterativeVariant::D1, count, 1U}};
    operation.UploadInitialState(input);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveFinalState(), expected.finalState);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), 1U);
}

TEST(Ex2CudaD1, KZeroReturnsUploadedStateWithoutCompute)
{
    cuda::Ex2CudaD1Operation operation{
        RequireCudaDevice(),
        {ex2::IterativeVariant::D1, InitialFixture.size(), 0U}};
    operation.UploadInitialState(InitialFixture);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveFinalState(), AsVector(InitialFixture));
    EXPECT_EQ(operation.CompletedFinalBuffer(), ex2::IterativeFinalBuffer::StateA);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), 0U);
    EXPECT_FALSE(operation.LastCompletionExecutedSequence());
}

TEST(Ex2CudaD1, NZeroPreservesEmptyStateAndParity)
{
    const std::vector<std::uint32_t> empty;
    for (const std::uint64_t iterations : {0U, 1U})
    {
        cuda::Ex2CudaD1Operation operation{
            RequireCudaDevice(),
            {ex2::IterativeVariant::D1, 0U, iterations}};
        operation.UploadInitialState(empty);
        operation.SubmitSequence();
        operation.WaitForCompletion();
        EXPECT_TRUE(operation.RetrieveFinalState().empty());
        EXPECT_EQ(operation.CompletedFinalBuffer(),
            iterations == 0U
                ? ex2::IterativeFinalBuffer::StateA
                : ex2::IterativeFinalBuffer::StateB);
        EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), 0U);
        EXPECT_FALSE(operation.LastCompletionExecutedSequence());
    }
}

TEST(Ex2CudaD1, LifecycleRejectsOutOfOrderAndDuplicateSubmission)
{
    cuda::Ex2CudaD1Operation operation{
        RequireCudaDevice(),
        {ex2::IterativeVariant::D1, InitialFixture.size(), 1U}};
    EXPECT_THROW(operation.SubmitSequence(), std::logic_error);
    EXPECT_THROW(operation.WaitForCompletion(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveFinalState()), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.CompletedFinalBuffer()), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.LastCompletionNativeDispatchCount()), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.LastCompletionExecutedSequence()), std::logic_error);

    operation.UploadInitialState(InitialFixture);
    operation.SubmitSequence();
    EXPECT_THROW(operation.SubmitSequence(), std::logic_error);
    EXPECT_THROW(static_cast<void>(operation.RetrieveFinalState()), std::logic_error);
    operation.WaitForCompletion();
    EXPECT_THROW(operation.SubmitSequence(), std::logic_error);
}

TEST(Ex2CudaD1, FailedReplacementUploadInvalidatesPriorReadiness)
{
    cuda::Ex2CudaD1Operation operation{
        RequireCudaDevice(),
        {ex2::IterativeVariant::D1, InitialFixture.size(), 1U}};
    operation.UploadInitialState(InitialFixture);
    const std::array<std::uint32_t, 3U> wrongLength{};
    EXPECT_THROW(operation.UploadInitialState(wrongLength), std::invalid_argument);
    EXPECT_THROW(operation.SubmitSequence(), std::logic_error);
    EXPECT_THROW(operation.UploadInitialState(InitialFixture), std::logic_error);
}

TEST(Ex2CudaD1, StaleRuntimeErrorDoesNotContaminateSequenceLaunchAttribution)
{
    cuda::Ex2CudaD1Operation operation{
        RequireCudaDevice(),
        {ex2::IterativeVariant::D1, InitialFixture.size(), 2U}};
    operation.UploadInitialState(InitialFixture);
    static_cast<void>(cudaMemcpyAsync(
        nullptr,
        nullptr,
        sizeof(std::uint32_t),
        cudaMemcpyDeviceToDevice,
        nullptr));
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveFinalState(), AsVector(KTwoFixture));
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), 2U);
}

TEST(Ex2CudaD1, CoreN262144K16MatchesCpuAndRepeats)
{
    RunCore(262'144U, 16U);
}

TEST(Ex2CudaD1, CoreN1048576K64MatchesCpuAndRepeats)
{
    RunCore(1'048'576U, 64U);
}

TEST(Ex2CudaD1, TimedOperationProducesCorrectEnvelopeWithoutPerPassMarkers)
{
    constexpr std::uint64_t count = 257U;
    constexpr std::uint64_t iterations = 3U;
    const auto input = ex2::GenerateWordInput(ex2::CoreInputSeed, count);
    const auto expected = ex2::ReferenceD1(input, iterations);
    cuda::Ex2CudaD1DeviceTimedOperation operation{
        RequireCudaDevice(),
        {ex2::IterativeVariant::D1, count, iterations}};
    operation.UploadInitialState(input);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.RetrieveFinalState(), expected.finalState);
    EXPECT_EQ(operation.CompletedFinalBuffer(), expected.finalBuffer);
    EXPECT_EQ(operation.LastCompletionNativeDispatchCount(), iterations);
    EXPECT_TRUE(operation.LastCompletionExecutedSequence());
    EXPECT_EQ(operation.NativeTimingStatus(),
        cuda::Ex2CudaD1NativeTimingStatus::Valid);
    EXPECT_TRUE(operation.NativeDeviceIntervalNanoseconds().has_value());
    EXPECT_EQ(operation.NativeTimingMetadata().method, "cudaEventElapsedTime");
}

TEST(Ex2CudaD1, TimedNoWorkIsNotApplicable)
{
    cuda::Ex2CudaD1DeviceTimedOperation operation{
        RequireCudaDevice(),
        {ex2::IterativeVariant::D1, InitialFixture.size(), 0U}};
    operation.UploadInitialState(InitialFixture);
    operation.SubmitSequence();
    operation.WaitForCompletion();
    EXPECT_EQ(operation.NativeTimingStatus(),
        cuda::Ex2CudaD1NativeTimingStatus::NotApplicable);
    EXPECT_FALSE(operation.NativeDeviceIntervalNanoseconds().has_value());
}

TEST(Ex2CudaD1Pure, ConfigurationLaunchResourceAndFailureSeams)
{
    static_assert(!cuda::Ex2CudaD1Operation::EnqueuesDeviceTimestamps);
    static_assert(cuda::Ex2CudaD1DeviceTimedOperation::EnqueuesDeviceTimestamps);
    ex2::WorkloadConfiguration valid = ex2::MakeConfiguration(
        ex2::IterativeConfiguration{ex2::IterativeVariant::D1, 257U, 2U});
    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaD1Configuration(valid));
    valid.common.executionMode = ex2::LogicalExecutionMode::Prepared;
    EXPECT_THROW(cuda::detail::ValidateEx2CudaD1Configuration(valid), std::invalid_argument);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaD1Configuration(
        ex2::MakeConfiguration(ex2::IterativeConfiguration{
            ex2::IterativeVariant::D2, 257U, 2U})), std::invalid_argument);

    const auto shape = cuda::detail::ValidateEx2CudaD1LaunchShape(
        {ex2::IterativeVariant::D1, 257U, 2U}, 2U, 256U, 256U);
    EXPECT_EQ(shape.blockCount, 2U);
    EXPECT_THROW(static_cast<void>(cuda::detail::ValidateEx2CudaD1LaunchShape(
        {ex2::IterativeVariant::D1, 257U, 2U}, 1U, 256U, 256U)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(cuda::detail::CalculateEx2CudaD1BufferByteCount(
        std::numeric_limits<std::uint32_t>::max() + std::uint64_t{1U},
        std::numeric_limits<std::size_t>::max())), std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        cuda::detail::CalculateEx2CudaD1BufferByteCount(2U, 7U)),
        std::length_error);
    EXPECT_THROW(cuda::detail::ValidateEx2CudaD1HostVectorCapacity(3U, 2U),
        std::length_error);
    EXPECT_NO_THROW(cuda::detail::ValidateEx2CudaD1MemoryFeasibility(4U, 8U, 8U));
    EXPECT_THROW(cuda::detail::ValidateEx2CudaD1MemoryFeasibility(5U, 8U, 8U),
        std::length_error);

    const auto partial = cuda::detail::ClassifyEx2CudaD1PassFailure(
        3U, 3U, false);
    EXPECT_EQ(partial.state,
        cuda::detail::Ex2CudaD1OperationState::CompletionUncertain);
    EXPECT_EQ(partial.attemptedPass, 3U);
    EXPECT_EQ(partial.acceptedDispatchCount, 3U);
    const auto safe = cuda::detail::ClassifyEx2CudaD1PassFailure(0U, 0U, true);
    EXPECT_EQ(safe.state, cuda::detail::Ex2CudaD1OperationState::Failed);
    const auto retained = cuda::detail::ClassifyEx2CudaD1CompletionDisposition(
        true, true);
    EXPECT_EQ(retained.deviceResources,
        cuda::detail::Ex2CudaD1ResourceDisposition::PreserveForProcessTeardown);
    EXPECT_EQ(retained.hostStorage,
        cuda::detail::Ex2CudaD1HostStorageDisposition::PreserveForProcessTeardown);
    EXPECT_FALSE(retained.operationReusable);
    EXPECT_FALSE(cuda::detail::IsEx2CudaD1OperationStateReusable(
        cuda::detail::Ex2CudaD1OperationState::CompletionUncertain));
    EXPECT_EQ(cuda::detail::Ex2CudaD1MillisecondsToNanoseconds(0.0005F), 500U);
    EXPECT_THROW(static_cast<void>(
        cuda::detail::Ex2CudaD1MillisecondsToNanoseconds(-1.0F)),
        std::invalid_argument);
}

TEST(Ex2CudaD1, InvalidDeviceAndD2AreRejected)
{
    EXPECT_THROW(cuda::Ex2CudaD1Operation(
        std::numeric_limits<int>::max(),
        {ex2::IterativeVariant::D1, 1U, 1U}),
        cuda::Ex2CudaD1NativeError);
    EXPECT_THROW(cuda::Ex2CudaD1Operation(
        RequireCudaDevice(),
        {ex2::IterativeVariant::D2, 1U, 1U}),
        std::invalid_argument);
}

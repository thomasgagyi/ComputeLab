#include "cuda/CudaQualification.hpp"
#include "input/SeededInput.hpp"
#include "oracle/DeterministicTransform.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

namespace cuda = computelab::cuda;

void ExpectValidHostTiming(const computelab::ex2::HostTimingIntervals& timing)
{
    EXPECT_EQ(timing.status, computelab::ex2::HostTimingStatus::Ok);
    ASSERT_TRUE(timing.hostSubmissionNanoseconds.has_value());
    ASSERT_TRUE(timing.hostWaitNanoseconds.has_value());
    ASSERT_TRUE(timing.hostCompletionNanoseconds.has_value());
    EXPECT_EQ(
        *timing.hostSubmissionNanoseconds + *timing.hostWaitNanoseconds,
        *timing.hostCompletionNanoseconds);
}

TEST(CudaQualification, TinyTransformExactlyMatchesCpuOracleAndG001Timing)
{
    const auto input = computelab::GenerateSeededInput(0xC0FFEEU, 257U);
    cuda::CudaQualificationOperation operation{0, input.size()};
    operation.Upload(input);

    const auto execution = operation.ExecuteHostOnly();

    EXPECT_EQ(execution.status, cuda::CudaQualificationStatus::Ok);
    EXPECT_EQ(
        execution.failurePhase,
        cuda::CudaQualificationFailurePhase::None);
    EXPECT_TRUE(execution.validationPassed);
    EXPECT_EQ(execution.output, computelab::TransformSequence(input));
    EXPECT_FALSE(execution.nativeErrorCode.has_value());
    EXPECT_EQ(
        execution.nativeTimingStatus,
        cuda::CudaNativeTimingStatus::NotApplicable);
    EXPECT_FALSE(execution.nativeTimingMetadata.has_value());
    ExpectValidHostTiming(execution.hostTiming);
}

TEST(CudaQualification, HostOnlyModeDeclaresAndEnqueuesNoDeviceTimestamps)
{
    EXPECT_EQ(cuda::CudaQualificationOperation::InstrumentMode, "H");
    EXPECT_FALSE(cuda::CudaQualificationOperation::EnqueuesDeviceTimestamps);
}

TEST(CudaQualification, HostAndNativeModesAreDistinctInstrumentConditions)
{
    EXPECT_EQ(cuda::CudaQualificationOperation::InstrumentMode, "H");
    EXPECT_FALSE(cuda::CudaQualificationOperation::EnqueuesDeviceTimestamps);
    EXPECT_EQ(
        cuda::CudaDeviceTimedQualificationOperation::InstrumentMode, "N");
    EXPECT_TRUE(
        cuda::CudaDeviceTimedQualificationOperation::EnqueuesDeviceTimestamps);
}

TEST(CudaQualificationTiming, ConvertsElapsedMillisecondsToIntegerNanoseconds)
{
    using cuda::detail::QualificationDeviceMillisecondsToNanoseconds;
    EXPECT_EQ(QualificationDeviceMillisecondsToNanoseconds(0.0F), 0U);
    EXPECT_EQ(QualificationDeviceMillisecondsToNanoseconds(1.0F), 1'000'000U);
    EXPECT_EQ(
        QualificationDeviceMillisecondsToNanoseconds(0.0009765625F),
        977U);
}

TEST(CudaQualificationTiming, RejectsInvalidAndUnrepresentableElapsedValues)
{
    using cuda::detail::QualificationDeviceMillisecondsToNanoseconds;
    EXPECT_THROW(
        static_cast<void>(QualificationDeviceMillisecondsToNanoseconds(
            std::numeric_limits<float>::quiet_NaN())),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(QualificationDeviceMillisecondsToNanoseconds(
            std::numeric_limits<float>::infinity())),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(QualificationDeviceMillisecondsToNanoseconds(-1.0F)),
        std::invalid_argument);
    EXPECT_THROW(
        static_cast<void>(QualificationDeviceMillisecondsToNanoseconds(
            std::numeric_limits<float>::max())),
        std::overflow_error);
}

TEST(CudaQualificationTiming, NativeTimingFailureStatusRetainsNoInterval)
{
    cuda::CudaQualificationExecution execution;
    execution.instrumentMode = "N";
    execution.nativeTimingStatus =
        cuda::CudaNativeTimingStatus::RetrievalFailed;
    EXPECT_EQ(execution.instrumentMode, "N");
    EXPECT_FALSE(execution.nativeDeviceIntervalNanoseconds.has_value());
    EXPECT_EQ(
        execution.nativeTimingStatus,
        cuda::CudaNativeTimingStatus::RetrievalFailed);
}

TEST(CudaQualification, OnlyEstablishedCompletionAllowsNormalResourceDestruction)
{
    using cuda::detail::CudaResourceDisposition;
    using cuda::detail::ClassifyCudaResourceDisposition;

    EXPECT_EQ(
        ClassifyCudaResourceDisposition(true),
        CudaResourceDisposition::DestroyNormally);
    EXPECT_EQ(
        ClassifyCudaResourceDisposition(false),
        CudaResourceDisposition::PreserveForProcessTeardown);
}

TEST(CudaQualification, ProvenCompleteOperationCanBeReusedWithoutAnotherUpload)
{
    const auto input = computelab::GenerateSeededInput(77U, 1'025U);
    const auto expected = computelab::TransformSequence(input);
    cuda::CudaQualificationOperation operation{0, input.size()};
    operation.Upload(input);

    const auto first = operation.ExecuteHostOnly();
    const auto second = operation.ExecuteHostOnly();

    ASSERT_EQ(first.status, cuda::CudaQualificationStatus::Ok);
    ASSERT_EQ(second.status, cuda::CudaQualificationStatus::Ok);
    EXPECT_EQ(first.output, expected);
    EXPECT_EQ(second.output, expected);
    ExpectValidHostTiming(first.hostTiming);
    ExpectValidHostTiming(second.hostTiming);
}

TEST(CudaQualification, DeviceTimedTransformMatchesOracleAndCanBeSafelyReused)
{
    const auto input = computelab::GenerateSeededInput(0xC0FFEEU, 257U);
    const auto expected = computelab::TransformSequence(input);
    cuda::CudaDeviceTimedQualificationOperation operation{0, input.size()};
    operation.Upload(input);

    const auto first = operation.ExecuteDeviceTimed();
    const auto second = operation.ExecuteDeviceTimed();

    ASSERT_EQ(first.status, cuda::CudaQualificationStatus::Ok);
    ASSERT_EQ(second.status, cuda::CudaQualificationStatus::Ok);
    EXPECT_EQ(first.instrumentMode, "N");
    EXPECT_EQ(first.output, expected);
    EXPECT_EQ(second.output, expected);
    EXPECT_TRUE(first.validationPassed);
    EXPECT_EQ(
        first.nativeTimingStatus,
        cuda::CudaNativeTimingStatus::Valid);
    EXPECT_TRUE(first.nativeDeviceIntervalNanoseconds.has_value());
    ASSERT_TRUE(first.nativeTimingMetadata.has_value());
    EXPECT_EQ(first.nativeTimingMetadata->method, "cudaEventElapsedTime");
    EXPECT_EQ(
        first.nativeTimingMetadata->eventCreateFlags,
        "cudaEventDefault (timing enabled)");
    EXPECT_EQ(
        first.nativeTimingMetadata->approximateResolutionNanoseconds,
        500U);
    ExpectValidHostTiming(first.hostTiming);
    ExpectValidHostTiming(second.hostTiming);
}

TEST(CudaQualification, InvalidLifecycleAndUploadSizeAreRejectedBeforeSubmission)
{
    cuda::CudaQualificationOperation operation{0, 1U};
    EXPECT_THROW(
        static_cast<void>(operation.ExecuteHostOnly()),
        std::logic_error);
    EXPECT_THROW(operation.Upload({}), std::invalid_argument);

    const std::vector<std::uint32_t> input{123U};
    operation.Upload(input);
    EXPECT_EQ(operation.ExecuteHostOnly().status, cuda::CudaQualificationStatus::Ok);

    cuda::CudaDeviceTimedQualificationOperation timedOperation{0, 1U};
    EXPECT_THROW(
        static_cast<void>(timedOperation.ExecuteDeviceTimed()),
        std::logic_error);
    EXPECT_THROW(timedOperation.Upload({}), std::invalid_argument);
    timedOperation.Upload(input);
    EXPECT_EQ(
        timedOperation.ExecuteDeviceTimed().status,
        cuda::CudaQualificationStatus::Ok);
}

TEST(CudaQualification, NativeSetupFailureRetainsOperationContext)
{
    try
    {
        cuda::CudaQualificationOperation operation{-1, 1U};
        FAIL() << "expected invalid CUDA device selection to fail";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(
            std::string{error.what()}.find("cudaSetDevice"),
            std::string::npos);
        EXPECT_NE(
            std::string{error.what()}.find("code"),
            std::string::npos);
    }

    try
    {
        cuda::CudaDeviceTimedQualificationOperation operation{-1, 1U};
        FAIL() << "expected invalid CUDA mode-N device selection to fail";
    }
    catch (const std::runtime_error& error)
    {
        EXPECT_NE(
            std::string{error.what()}.find("cudaSetDevice"),
            std::string::npos);
        EXPECT_NE(
            std::string{error.what()}.find("code"),
            std::string::npos);
    }
}

} // namespace

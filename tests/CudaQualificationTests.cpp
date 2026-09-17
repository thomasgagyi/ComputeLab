#include "cuda/CudaQualification.hpp"
#include "input/SeededInput.hpp"
#include "oracle/DeterministicTransform.hpp"

#include <gtest/gtest.h>

#include <cstdint>
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
    ExpectValidHostTiming(execution.hostTiming);
}

TEST(CudaQualification, HostOnlyModeDeclaresAndEnqueuesNoDeviceTimestamps)
{
    EXPECT_EQ(cuda::CudaQualificationOperation::InstrumentMode, "H");
    EXPECT_FALSE(cuda::CudaQualificationOperation::EnqueuesDeviceTimestamps);
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
}

} // namespace

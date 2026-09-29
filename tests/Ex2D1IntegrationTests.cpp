#include "app/Ex2D1Integration.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#ifndef COMPUTELAB_EX2_D1_SPIRV_PATH
#error COMPUTELAB_EX2_D1_SPIRV_PATH must name the generated D1 shader
#endif

namespace
{

namespace cuda = computelab::cuda;
namespace d1 = computelab::ex2::d1;
namespace ex2 = computelab::ex2;
namespace vulkan = computelab::vulkan;

const std::filesystem::path SpirvPath{COMPUTELAB_EX2_D1_SPIRV_PATH};

constexpr std::array<std::uint32_t, 4U> InitialFixture{
    0xA48FAA9DU, 0xC374CA1AU, 0x3BECCAA2U, 0x912DCEF8U};
constexpr std::array<std::uint32_t, 4U> KOneFixture{
    0xD664E09CU, 0x27C0F020U, 0x3AC0DF49U, 0x62A16496U};

std::vector<std::uint32_t> AsVector(
    const std::array<std::uint32_t, 4U>& values)
{
    return {values.begin(), values.end()};
}

void ExpectFailure(
    const d1::FailureClassification& actual,
    d1::IntegrationStatus status,
    d1::IntegrationFailurePhase phase,
    d1::IntegrationErrorCode error)
{
    EXPECT_EQ(actual.status, status);
    EXPECT_EQ(actual.phase, phase);
    EXPECT_EQ(actual.errorCode, error);
}

void ExpectPassingObservation(
    const d1::CrossBackendObservation& observation,
    std::uint64_t elementCount,
    std::uint64_t iterationCount,
    ex2::IterativeFinalBuffer finalBuffer)
{
    ASSERT_TRUE(observation.Passed());
    EXPECT_TRUE(observation.physicalIdentityVerified);
    EXPECT_FALSE(d1::IsZeroDeviceUuid(observation.verifiedDeviceUuid));
    EXPECT_EQ(
        observation.verifiedDeviceUuid,
        observation.vulkanDiagnostics.deviceUuid);
    EXPECT_EQ(observation.initialState.size(), elementCount);
    EXPECT_EQ(observation.expectedFinalState.size(), elementCount);
    EXPECT_EQ(observation.expectedFinalBuffer, finalBuffer);
    EXPECT_EQ(observation.initialStateSha256.size(), 64U);
    EXPECT_EQ(observation.expectedFinalStateSha256.size(), 64U);
    EXPECT_TRUE(observation.vulkanShaderProvenanceVerified);
    EXPECT_EQ(observation.vulkanLoadedShaderName, "Ex2D1.comp.spv");
    EXPECT_EQ(
        observation.vulkanLoadedSpirvSha256,
        observation.vulkanShaderFileSha256);
    EXPECT_FALSE(observation.vulkanDiagnostics.timestampQueryPoolCreated);
    EXPECT_EQ(observation.vulkanDiagnostics.validationErrorCount, 0U);
    const char* validationMode = std::getenv("COMPUTELAB_EX2_D1_VALIDATION");
    EXPECT_EQ(observation.vulkanDiagnostics.validationEnabled,
        validationMode != nullptr);
    EXPECT_EQ(observation.vulkanDiagnostics.synchronizationValidationEnabled,
        validationMode != nullptr
            && std::string_view{validationMode} == "sync");

    for (const auto* backend : {&observation.cuda, &observation.vulkan})
    {
        EXPECT_TRUE(backend->nativeSequenceCompleted);
        EXPECT_TRUE(backend->finalStateObserved);
        EXPECT_TRUE(backend->cpuComparisonPerformed);
        EXPECT_EQ(backend->validationPassed, true);
        EXPECT_EQ(backend->status, d1::IntegrationStatus::Ok);
        EXPECT_TRUE(backend->safeForFurtherGpuCalls);
        EXPECT_TRUE(backend->nativeSequenceExecuted);
        EXPECT_TRUE(backend->hModeUninstrumented);
        EXPECT_EQ(backend->expectedFinalBuffer, finalBuffer);
        EXPECT_EQ(backend->completedFinalBuffer, finalBuffer);
        EXPECT_EQ(backend->expectedIterationCount, iterationCount);
        EXPECT_EQ(backend->completedNativeDispatchCount, iterationCount);
        EXPECT_EQ(backend->finalState, observation.expectedFinalState);
    }
    EXPECT_FALSE(observation.cuda.completedInterPassBarrierCount.has_value());
    EXPECT_EQ(
        observation.vulkan.completedInterPassBarrierCount,
        static_cast<std::uint32_t>(iterationCount - 1U));
    EXPECT_TRUE(observation.pairComparisonPerformed);
    EXPECT_TRUE(observation.pairFinalStatesEqual);
    EXPECT_EQ(observation.cuda.finalState, observation.vulkan.finalState);
}

TEST(Ex2D1IntegrationIdentity,
    RejectsZeroUnequalAndOneByteMismatchAndFormatsLowercase)
{
    d1::DeviceUuid zero{};
    d1::DeviceUuid uuid{
        0x00U, 0x11U, 0x22U, 0x33U,
        0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xAAU, 0xBBU,
        0xCCU, 0xDDU, 0xEEU, 0xFFU};
    auto oneByteMismatch = uuid;
    oneByteMismatch[7] ^= 0x01U;

    EXPECT_TRUE(d1::IsZeroDeviceUuid(zero));
    EXPECT_FALSE(d1::IsZeroDeviceUuid(uuid));
    EXPECT_THROW(static_cast<void>(d1::FormatDeviceUuid(zero)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(d1::VerifySamePhysicalDevice(zero, uuid)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(d1::VerifySamePhysicalDevice(uuid, zero)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(
        d1::VerifySamePhysicalDevice(uuid, oneByteMismatch)),
        std::invalid_argument);
    EXPECT_EQ(
        d1::VerifySamePhysicalDevice(uuid, uuid),
        "00112233-4455-6677-8899-aabbccddeeff");
}

TEST(Ex2D1IntegrationConfiguration,
    ValidatesDeclaredStateAndExactTwoApprovedCoreCells)
{
    const auto first = ex2::MakeConfiguration(ex2::IterativeConfiguration{
        ex2::IterativeVariant::D1, 262'144U, 16U});
    const auto second = ex2::MakeConfiguration(ex2::IterativeConfiguration{
        ex2::IterativeVariant::D1, 1'048'576U, 64U});
    EXPECT_EQ(ex2::ApprovedCoreCells().size(), 22U);
    EXPECT_EQ(std::count_if(
        ex2::ApprovedCoreCells().begin(),
        ex2::ApprovedCoreCells().end(),
        [](const ex2::WorkloadConfiguration& configuration) {
            const auto* iterative = std::get_if<ex2::IterativeConfiguration>(
                &configuration.parameters);
            return iterative != nullptr
                && iterative->variant == ex2::IterativeVariant::D1;
        }), 2);
    EXPECT_EQ(ex2::ClassifyCellEligibility(first),
        ex2::CellEligibility::ApprovedCoreCell);
    EXPECT_EQ(ex2::ClassifyCellEligibility(second),
        ex2::CellEligibility::ApprovedCoreCell);

    const auto exact = ex2::GenerateWordInput(ex2::CoreInputSeed, 4U);
    const auto configuration = ex2::MakeConfiguration(
        ex2::IterativeConfiguration{ex2::IterativeVariant::D1, 4U, 1U});
    EXPECT_NO_THROW(d1::ValidateDeclaredInitialState(configuration, exact));
    auto shortState = exact;
    shortState.pop_back();
    EXPECT_THROW(d1::ValidateDeclaredInitialState(configuration, shortState),
        std::invalid_argument);
    auto wrongSeed = configuration;
    ++wrongSeed.common.seed;
    EXPECT_THROW(d1::ValidateDeclaredInitialState(wrongSeed, exact),
        std::invalid_argument);
    EXPECT_THROW(d1::ValidateDeclaredInitialState(
        ex2::MakeConfiguration(ex2::IterativeConfiguration{
            ex2::IterativeVariant::D2, 4U, 1U}), exact),
        std::invalid_argument);
}

TEST(Ex2D1IntegrationValidation,
    DetectsLengthWordsParityDispatchBarriersExecutionAndInstrumentation)
{
    const auto expected = AsVector(KOneFixture);
    const auto valid = d1::ValidateCompletedResult(
        expected,
        expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        1U,
        1U,
        0U,
        0U,
        true,
        true);
    EXPECT_TRUE(valid.Passed());

    auto shortState = expected;
    shortState.pop_back();
    EXPECT_FALSE(d1::ValidateCompletedResult(
        expected, shortState,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        1U, 1U, 0U, 0U, true, true).finalStateLengthMatches);

    auto altered = expected;
    altered[2] ^= 1U;
    EXPECT_FALSE(d1::ValidateCompletedResult(
        expected, altered,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        1U, 1U, 0U, 0U, true, true).finalStateMatchesExpected);
    EXPECT_FALSE(d1::ValidateCompletedResult(
        expected, expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateA,
        ex2::IterativeFinalBuffer::StateB,
        1U, 1U, 0U, 0U, true, true).operationExpectedBufferMatches);
    EXPECT_FALSE(d1::ValidateCompletedResult(
        expected, expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateA,
        1U, 1U, 0U, 0U, true, true).completedBufferMatches);
    EXPECT_FALSE(d1::ValidateCompletedResult(
        expected, expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        1U, 0U, 0U, 0U, true, true).dispatchCountMatches);
    EXPECT_FALSE(d1::ValidateCompletedResult(
        expected, expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        1U, 1U, 0U, 1U, true, true).interPassBarrierCountMatches);
    EXPECT_FALSE(d1::ValidateCompletedResult(
        expected, expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        1U, 1U, 0U, 0U, false, true).nativeSequenceMatches);
    EXPECT_FALSE(d1::ValidateCompletedResult(
        expected, expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        1U, 1U, 0U, 0U, true, false).hModeUninstrumented);
}

TEST(Ex2D1IntegrationValidation,
    CompletedObservationUsesSpecificKnownSafeValidationErrors)
{
    const auto expected = AsVector(KOneFixture);
    const auto parity = d1::CompletedObservation(
        expected, expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateA,
        ex2::IterativeFinalBuffer::StateB,
        1U, 1U, std::nullopt, std::nullopt, true, true);
    EXPECT_EQ(parity.status, d1::IntegrationStatus::ValidationFailed);
    EXPECT_EQ(parity.errorCode, d1::IntegrationErrorCode::FinalBufferMismatch);
    EXPECT_TRUE(parity.safeForFurtherGpuCalls);

    const auto dispatch = d1::CompletedObservation(
        expected, expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        1U, 0U, std::nullopt, std::nullopt, true, true);
    EXPECT_EQ(dispatch.errorCode,
        d1::IntegrationErrorCode::DispatchCountMismatch);

    const auto barrier = d1::CompletedObservation(
        expected, expected,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        ex2::IterativeFinalBuffer::StateB,
        1U, 1U, 0U, 1U, true, true);
    EXPECT_EQ(barrier.errorCode,
        d1::IntegrationErrorCode::InterPassBarrierCountMismatch);
}

TEST(Ex2D1IntegrationPair,
    RequiresBothCpuComparisonsAndNeverRewritesBackendTruth)
{
    d1::CrossBackendObservation observation;
    observation.cuda.cpuComparisonPerformed = true;
    observation.cuda.status = d1::IntegrationStatus::Ok;
    observation.cuda.validationPassed = true;
    observation.cuda.finalState = {1U, 2U};
    observation.vulkan.status = d1::IntegrationStatus::ValidationFailed;
    observation.vulkan.validationPassed = false;
    observation.vulkan.finalState = {1U, 3U};

    d1::ReconcileCrossBackendFinalStates(observation);
    EXPECT_FALSE(observation.pairComparisonPerformed);
    observation.vulkan.cpuComparisonPerformed = true;
    d1::ReconcileCrossBackendFinalStates(observation);
    EXPECT_TRUE(observation.pairComparisonPerformed);
    EXPECT_FALSE(observation.pairFinalStatesEqual);
    EXPECT_EQ(observation.cuda.status, d1::IntegrationStatus::Ok);
    EXPECT_EQ(observation.cuda.validationPassed, true);
    EXPECT_EQ(observation.vulkan.status,
        d1::IntegrationStatus::ValidationFailed);
    EXPECT_EQ(observation.vulkan.validationPassed, false);
}

TEST(Ex2D1IntegrationFailureSession,
    InterruptedObservationAndResourceDispositionPreserveSafetyTruth)
{
    const auto safe = d1::MakeInterruptedObservation(true);
    EXPECT_EQ(safe.status, d1::IntegrationStatus::Incomplete);
    EXPECT_EQ(safe.failurePhase,
        d1::IntegrationFailurePhase::InterruptedSecondBackend);
    EXPECT_EQ(safe.errorCode, d1::IntegrationErrorCode::Interrupted);
    EXPECT_TRUE(safe.safeForFurtherGpuCalls);
    EXPECT_FALSE(safe.nativeSequenceCompleted);
    EXPECT_FALSE(safe.finalStateObserved);
    EXPECT_FALSE(safe.cpuComparisonPerformed);
    EXPECT_FALSE(safe.validationPassed.has_value());
    EXPECT_EQ(d1::ClassifyFailedSessionResourceDisposition(safe),
        d1::FailedSessionResourceDisposition::DestroyNormally);

    const auto unsafe = d1::MakeInterruptedObservation(false);
    EXPECT_FALSE(unsafe.safeForFurtherGpuCalls);
    EXPECT_EQ(d1::ClassifyFailedSessionResourceDisposition(unsafe),
        d1::FailedSessionResourceDisposition::PreserveForProcessTeardown);
}

TEST(Ex2D1IntegrationCudaClassification, CoversEveryCurrentNativePhase)
{
    using Error = d1::IntegrationErrorCode;
    using Native = cuda::Ex2CudaD1NativePhase;
    using Phase = d1::IntegrationFailurePhase;
    using Status = d1::IntegrationStatus;
    struct Case
    {
        Native native;
        Status status;
        Phase phase;
        Error error;
    };
    const std::array cases{
        Case{Native::DeviceSelection, Status::Incomplete,
            Phase::BackendInitialization, Error::BackendInitializationFailed},
        Case{Native::RuntimeInitialization, Status::Incomplete,
            Phase::BackendInitialization, Error::BackendInitializationFailed},
        Case{Native::DeviceProperties, Status::Incomplete,
            Phase::BackendInitialization, Error::BackendInitializationFailed},
        Case{Native::ResourcePreflight, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::StreamCreation, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::StateAAllocation, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::StateBAllocation, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::InitialUpload, Status::Incomplete,
            Phase::InitialUpload, Error::InitialUploadFailed},
        Case{Native::UploadCompletion, Status::Incomplete,
            Phase::UploadCompletion, Error::UploadCompletionFailed},
        Case{Native::StartMarker, Status::Incomplete,
            Phase::StartMarker, Error::StartMarkerFailed},
        Case{Native::SequenceSubmission, Status::SubmitFailed,
            Phase::SequenceSubmission, Error::SubmissionFailed},
        Case{Native::StopMarker, Status::Incomplete,
            Phase::StopMarker, Error::StopMarkerFailed},
        Case{Native::CompletionWait, Status::WaitFailed,
            Phase::CompletionWait, Error::CompletionFailed},
        Case{Native::FinalReadback, Status::Incomplete,
            Phase::FinalReadback, Error::FinalReadbackFailed},
        Case{Native::NativeTimingRetrieval, Status::Incomplete,
            Phase::NativeTimingRetrieval, Error::NativeTimingRetrievalFailed},
        Case{Native::NativeTimingConversion, Status::Incomplete,
            Phase::NativeTimingConversion, Error::NativeTimingConversionFailed}};

    for (const auto& test : cases)
    {
        ExpectFailure(d1::ClassifyCudaFailure(test.native, 1),
            test.status, test.phase, test.error);
    }
    ExpectFailure(d1::ClassifyCudaFailure(static_cast<Native>(999), 1),
        Status::Incomplete, Phase::NativeFailure,
        Error::UnclassifiedNativeFailure);

    const auto partial = d1::ClassifyCudaFailure(
        Native::SequenceSubmission, 1, 3U, 3U);
    EXPECT_EQ(partial.attemptedPass, 3U);
    EXPECT_EQ(partial.acceptedDispatchCount, 3U);
}

TEST(Ex2D1IntegrationVulkanClassification, CoversEveryCurrentNativePhase)
{
    using Error = d1::IntegrationErrorCode;
    using Native = vulkan::Ex2VulkanD1NativePhase;
    using Phase = d1::IntegrationFailurePhase;
    using Status = d1::IntegrationStatus;
    struct Case
    {
        Native native;
        Status status;
        Phase phase;
        Error error;
    };
    const std::array cases{
        Case{Native::InstanceCreation, Status::Incomplete,
            Phase::BackendInitialization, Error::BackendInitializationFailed},
        Case{Native::DeviceEnumeration, Status::Incomplete,
            Phase::BackendInitialization, Error::BackendInitializationFailed},
        Case{Native::DeviceSelection, Status::Incomplete,
            Phase::BackendInitialization, Error::BackendInitializationFailed},
        Case{Native::DeviceProperties, Status::Incomplete,
            Phase::BackendInitialization, Error::BackendInitializationFailed},
        Case{Native::QueueSelection, Status::Incomplete,
            Phase::BackendInitialization, Error::BackendInitializationFailed},
        Case{Native::LogicalDeviceCreation, Status::Incomplete,
            Phase::BackendInitialization, Error::BackendInitializationFailed},
        Case{Native::ResourcePreflight, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::BufferCreation, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::MemoryAllocation, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::MemoryBinding, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::MemoryMapping, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::DescriptorCreation, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::PipelineConstruction, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::CommandCreation, Status::Incomplete,
            Phase::ResourceAllocation, Error::ResourceAllocationFailed},
        Case{Native::InitialUpload, Status::Incomplete,
            Phase::InitialUpload, Error::InitialUploadFailed},
        Case{Native::CommandBufferReset, Status::Incomplete,
            Phase::CommandBufferReset, Error::CommandBufferResetFailed},
        Case{Native::CommandRecording, Status::Incomplete,
            Phase::CommandRecording, Error::CommandRecordingFailed},
        Case{Native::Submission, Status::SubmitFailed,
            Phase::SequenceSubmission, Error::SubmissionFailed},
        Case{Native::CompletionWait, Status::WaitFailed,
            Phase::CompletionWait, Error::CompletionFailed},
        Case{Native::FinalReadback, Status::Incomplete,
            Phase::FinalReadback, Error::FinalReadbackFailed},
        Case{Native::QueryReset, Status::Incomplete,
            Phase::QueryReset, Error::QueryResetFailed},
        Case{Native::NativeTimingRetrieval, Status::Incomplete,
            Phase::NativeTimingRetrieval, Error::NativeTimingRetrievalFailed},
        Case{Native::NativeTimingConversion, Status::Incomplete,
            Phase::NativeTimingConversion, Error::NativeTimingConversionFailed}};

    for (const auto& test : cases)
    {
        ExpectFailure(d1::ClassifyVulkanFailure(test.native, VK_ERROR_UNKNOWN),
            test.status, test.phase, test.error);
    }
    ExpectFailure(d1::ClassifyVulkanFailure(
        static_cast<Native>(999), VK_ERROR_UNKNOWN),
        Status::Incomplete, Phase::NativeFailure,
        Error::UnclassifiedNativeFailure);
}

TEST(Ex2D1IntegrationVulkanClassification,
    TimeoutAndDeviceLossOverlayWithoutErasingMappedPhase)
{
    using Error = d1::IntegrationErrorCode;
    using Native = vulkan::Ex2VulkanD1NativePhase;
    using Phase = d1::IntegrationFailurePhase;
    using Status = d1::IntegrationStatus;

    for (const auto [native, phase] : {
        std::pair{Native::InitialUpload, Phase::InitialUpload},
        std::pair{Native::CommandRecording, Phase::CommandRecording},
        std::pair{Native::CompletionWait, Phase::CompletionWait},
        std::pair{Native::FinalReadback, Phase::FinalReadback}})
    {
        ExpectFailure(d1::ClassifyVulkanFailure(native, VK_TIMEOUT),
            Status::Timeout, phase, Error::OperationTimeout);
    }
    for (const auto [native, phase] : {
        std::pair{Native::Submission, Phase::SequenceSubmission},
        std::pair{Native::CompletionWait, Phase::CompletionWait},
        std::pair{Native::FinalReadback, Phase::FinalReadback}})
    {
        ExpectFailure(d1::ClassifyVulkanFailure(
            native, VK_ERROR_DEVICE_LOST),
            Status::DeviceLost, phase, Error::DeviceLost);
    }
}

TEST(Ex2D1IntegrationLiteral,
    IndependentOddParityFixtureAnchorsCpuCudaVulkanAndPairEquality)
{
    const auto observation = d1::RunCrossBackendCorrectness(
        4U, 1U, 0, 0U, SpirvPath);
    const auto initial = AsVector(InitialFixture);
    const auto expected = AsVector(KOneFixture);

    ExpectPassingObservation(
        observation, 4U, 1U, ex2::IterativeFinalBuffer::StateB);
    EXPECT_EQ(observation.initialState, initial);
    EXPECT_EQ(observation.expectedFinalState, expected);
    EXPECT_EQ(ex2::ReferenceD1(initial, 1U).finalState, expected);
    EXPECT_EQ(observation.cuda.finalState, expected);
    EXPECT_EQ(observation.vulkan.finalState, expected);
    EXPECT_EQ(observation.cuda.completedNativeDispatchCount, 1U);
    EXPECT_EQ(observation.vulkan.completedNativeDispatchCount, 1U);
    EXPECT_EQ(observation.vulkan.completedInterPassBarrierCount, 0U);
    EXPECT_EQ(ex2::ClassifyCellEligibility(observation.configuration),
        ex2::CellEligibility::CorrectnessOnly);
    RecordProperty("literal_uuid", observation.verifiedDeviceUuidText);
    RecordProperty("literal_initial_sha256", observation.initialStateSha256);
    RecordProperty(
        "literal_expected_sha256", observation.expectedFinalStateSha256);
    RecordProperty("literal_spirv_sha256",
        observation.vulkanLoadedSpirvSha256);
}

TEST(Ex2D1IntegrationCore, BothApprovedCorePairsPassWithoutSkips)
{
    struct CoreCase
    {
        const char* label;
        std::uint64_t elementCount;
        std::uint64_t iterationCount;
    };
    constexpr std::array cases{
        CoreCase{"n262144_k16", 262'144U, 16U},
        CoreCase{"n1048576_k64", 1'048'576U, 64U}};

    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.label);
        const auto observation = d1::RunCrossBackendCorrectness(
            test.elementCount,
            test.iterationCount,
            0,
            0U,
            SpirvPath);
        ExpectPassingObservation(
            observation,
            test.elementCount,
            test.iterationCount,
            ex2::IterativeFinalBuffer::StateA);
        EXPECT_EQ(ex2::ClassifyCellEligibility(observation.configuration),
            ex2::CellEligibility::ApprovedCoreCell);
        EXPECT_EQ(observation.configuration.common.seed, ex2::CoreInputSeed);
        EXPECT_EQ(observation.configuration.common.generatorRevision,
            ex2::InputGeneratorRevision);
        EXPECT_EQ(observation.configuration.common.executionMode,
            ex2::LogicalExecutionMode::Ordinary);
        EXPECT_EQ(observation.configuration.common.operationBoundary,
            ex2::OperationBoundary::OrdinaryIterationSequenceCompletion);

        const std::string prefix = std::string{test.label} + "_";
        RecordProperty(
            (prefix + "uuid").c_str(), observation.verifiedDeviceUuidText);
        RecordProperty((prefix + "initial_sha256").c_str(),
            observation.initialStateSha256);
        RecordProperty((prefix + "expected_sha256").c_str(),
            observation.expectedFinalStateSha256);
        RecordProperty((prefix + "spirv_sha256").c_str(),
            observation.vulkanLoadedSpirvSha256);
    }
}

} // namespace

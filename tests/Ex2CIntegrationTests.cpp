#include "app/Ex2CIntegration.hpp"

#include "ex2/Ex2ContentionTargets.hpp"
#include "ex2/Ex2Input.hpp"
#include "ex2/Ex2Sha256.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

namespace c = computelab::ex2::c;
namespace cuda = computelab::cuda;
namespace ex2 = computelab::ex2;
namespace vulkan = computelab::vulkan;

void ExpectFailure(
    const c::FailureClassification& actual,
    c::IntegrationStatus status,
    c::IntegrationFailurePhase phase,
    c::IntegrationErrorCode error)
{
    EXPECT_EQ(actual.status, status);
    EXPECT_EQ(actual.phase, phase);
    EXPECT_EQ(actual.errorCode, error);
}

void VerifyLiteralOccupancy(
    const std::vector<std::uint32_t>& counters,
    std::uint64_t activeCounterCount,
    std::uint32_t expectedActiveValue)
{
    ASSERT_EQ(counters.size(), ex2::ContentionElementCount);
    for (std::size_t index = 0U; index < counters.size(); ++index)
    {
        ASSERT_EQ(
            counters[index],
            index < activeCounterCount ? expectedActiveValue : 0U)
            << "counter index " << index;
    }
    EXPECT_EQ(
        std::accumulate(counters.begin(), counters.end(), std::uint64_t{0}),
        ex2::ContentionElementCount);
}

TEST(Ex2CIntegrationIdentity, RejectsZeroAndUnequalUuidsAndFormatsEquality)
{
    c::DeviceUuid zero{};
    c::DeviceUuid uuid{
        0x00U, 0x11U, 0x22U, 0x33U,
        0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xaaU, 0xbbU,
        0xccU, 0xddU, 0xeeU, 0xffU};
    auto different = uuid;
    different.back() ^= 0x01U;

    EXPECT_TRUE(c::IsZeroDeviceUuid(zero));
    EXPECT_FALSE(c::IsZeroDeviceUuid(uuid));
    EXPECT_THROW(static_cast<void>(c::VerifySamePhysicalDevice(zero, uuid)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(c::VerifySamePhysicalDevice(uuid, zero)),
        std::invalid_argument);
    EXPECT_THROW(static_cast<void>(c::VerifySamePhysicalDevice(uuid, different)),
        std::invalid_argument);
    EXPECT_EQ(
        c::VerifySamePhysicalDevice(uuid, uuid),
        "00112233-4455-6677-8899-aabbccddeeff");
}

TEST(Ex2CIntegrationTargets,
    RejectsWrongLengthAndAlteredInRangeDeclaredTargets)
{
    const auto configuration = ex2::MakeConfiguration(
        ex2::ContentionConfiguration{8U, 2U, 8U});
    const auto exact = ex2::GenerateContentionTargets(8U, 2U);
    auto shortTargets = exact;
    shortTargets.pop_back();
    auto altered = exact;
    altered[0] = altered[0] == 0U ? 1U : 0U;

    EXPECT_NO_THROW(c::ValidateDeclaredTargets(configuration, exact));
    EXPECT_THROW(c::ValidateDeclaredTargets(configuration, shortTargets),
        std::invalid_argument);
    EXPECT_THROW(c::ValidateDeclaredTargets(configuration, altered),
        std::invalid_argument);
    EXPECT_THROW(c::ValidateDeclaredTargets(
        ex2::MakeConfiguration(
            ex2::LinearConfiguration{ex2::LinearVariant::A1, 8U}),
        exact), std::invalid_argument);
}

TEST(Ex2CIntegrationBuffers,
    DistinguishesCounterTargetAndDispatchValidationFailures)
{
    const std::vector<std::uint32_t> expected{1U, 1U};
    const std::vector<std::uint32_t> targets{1U, 0U};

    EXPECT_TRUE(c::ValidateBuffers(
        expected, expected, targets, targets, true).Passed());

    const auto shortCounters = c::ValidateBuffers(
        expected, std::vector<std::uint32_t>{2U}, targets, targets, true);
    EXPECT_FALSE(shortCounters.counterLengthMatches);
    EXPECT_FALSE(shortCounters.Passed());

    const auto wrongCounters = c::ValidateBuffers(
        expected, std::vector<std::uint32_t>{2U, 0U}, targets, targets, true);
    EXPECT_TRUE(wrongCounters.counterLengthMatches);
    EXPECT_FALSE(wrongCounters.countersMatchExpected);
    EXPECT_TRUE(wrongCounters.counterTotalMatches);
    EXPECT_FALSE(wrongCounters.Passed());

    const auto wrongTargets = c::ValidateBuffers(
        expected, expected, targets, std::vector<std::uint32_t>{0U, 1U}, true);
    EXPECT_FALSE(wrongTargets.targetsPreserved);
    EXPECT_FALSE(wrongTargets.Passed());

    const auto wrongDispatch = c::ValidateBuffers(
        expected, expected, targets, targets, false);
    EXPECT_FALSE(wrongDispatch.nativeDispatchMatches);
    EXPECT_FALSE(wrongDispatch.Passed());

    const auto targetFailure = c::CompletedObservation(
        expected,
        expected,
        targets,
        std::vector<std::uint32_t>{0U, 1U},
        true);
    ASSERT_TRUE(targetFailure.errorCode.has_value());
    EXPECT_EQ(*targetFailure.errorCode,
        c::IntegrationErrorCode::TargetPreservationFailed);
    EXPECT_TRUE(targetFailure.safeForFurtherGpuCalls);
}

TEST(Ex2CIntegrationPair,
    ComparesOnlyAfterBothCpuComparisonsWithoutRewritingBackendTruth)
{
    c::CrossBackendObservation observation;
    observation.cuda.cpuComparisonPerformed = true;
    observation.cuda.status = c::IntegrationStatus::Ok;
    observation.cuda.validationPassed = true;
    observation.cuda.counters = {1U, 1U};
    observation.vulkan.status = c::IntegrationStatus::Ok;
    observation.vulkan.validationPassed = true;
    observation.vulkan.counters = {2U, 0U};

    c::ReconcileCrossBackendCounters(observation);
    EXPECT_FALSE(observation.pairComparisonPerformed);

    observation.vulkan.cpuComparisonPerformed = true;
    c::ReconcileCrossBackendCounters(observation);
    EXPECT_TRUE(observation.pairComparisonPerformed);
    EXPECT_FALSE(observation.pairCountersEqual);
    EXPECT_EQ(observation.cuda.status, c::IntegrationStatus::Ok);
    EXPECT_EQ(observation.vulkan.status, c::IntegrationStatus::Ok);
    EXPECT_EQ(observation.cuda.validationPassed, true);
    EXPECT_EQ(observation.vulkan.validationPassed, true);
}

TEST(Ex2CIntegrationFailureSession,
    InterruptedObservationAndResourceDispositionRemainConservative)
{
    const auto safe = c::MakeInterruptedObservation(true);
    EXPECT_EQ(safe.status, c::IntegrationStatus::Incomplete);
    EXPECT_EQ(safe.failurePhase,
        c::IntegrationFailurePhase::InterruptedSecondBackend);
    EXPECT_EQ(safe.errorCode, c::IntegrationErrorCode::Interrupted);
    EXPECT_TRUE(safe.safeForFurtherGpuCalls);
    EXPECT_FALSE(safe.nativeOperationCompleted);
    EXPECT_FALSE(safe.countersObserved);
    EXPECT_FALSE(safe.cpuComparisonPerformed);
    EXPECT_FALSE(safe.validationPassed.has_value());

    EXPECT_EQ(c::ClassifyFailedSessionResourceDisposition(safe),
        c::FailedSessionResourceDisposition::DestroyNormally);
    const auto unsafe = c::MakeInterruptedObservation(false);
    EXPECT_EQ(c::ClassifyFailedSessionResourceDisposition(unsafe),
        c::FailedSessionResourceDisposition::PreserveForProcessTeardown);
}

TEST(Ex2CIntegrationCudaClassification, CoversEveryCurrentNativePhase)
{
    using Error = c::IntegrationErrorCode;
    using IntegrationPhase = c::IntegrationFailurePhase;
    using Native = cuda::Ex2CudaCNativePhase;
    using Status = c::IntegrationStatus;
    struct Case
    {
        Native native;
        Status status;
        IntegrationPhase phase;
        Error error;
    };
    const std::array cases{
        Case{Native::DeviceSelection, Status::Incomplete,
            IntegrationPhase::BackendInitialization,
            Error::BackendInitializationFailed},
        Case{Native::RuntimeInitialization, Status::Incomplete,
            IntegrationPhase::BackendInitialization,
            Error::BackendInitializationFailed},
        Case{Native::DeviceProperties, Status::Incomplete,
            IntegrationPhase::BackendInitialization,
            Error::BackendInitializationFailed},
        Case{Native::ResourcePreflight, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::StreamCreation, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::TargetAllocation, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::CounterAllocation, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::TargetUpload, Status::Incomplete,
            IntegrationPhase::TargetUpload, Error::TargetUploadFailed},
        Case{Native::TargetUploadCompletion, Status::Incomplete,
            IntegrationPhase::UploadCompletion,
            Error::UploadCompletionFailed},
        Case{Native::CounterReset, Status::Incomplete,
            IntegrationPhase::CounterReset, Error::CounterResetFailed},
        Case{Native::ResetCompletion, Status::Incomplete,
            IntegrationPhase::ResetCompletion, Error::ResetCompletionFailed},
        Case{Native::Submission, Status::SubmitFailed,
            IntegrationPhase::Submission, Error::SubmissionFailed},
        Case{Native::CompletionWait, Status::WaitFailed,
            IntegrationPhase::CompletionWait, Error::CompletionFailed},
        Case{Native::CounterReadback, Status::Incomplete,
            IntegrationPhase::CounterReadback, Error::CounterReadbackFailed},
        Case{Native::TargetDiagnosticReadback, Status::Incomplete,
            IntegrationPhase::TargetDiagnosticReadback,
            Error::TargetReadbackFailed}};

    for (const auto& test : cases)
    {
        ExpectFailure(c::ClassifyCudaFailure(test.native, 1),
            test.status, test.phase, test.error);
    }
    ExpectFailure(c::ClassifyCudaFailure(static_cast<Native>(999), 1),
        Status::Incomplete,
        IntegrationPhase::NativeFailure,
        Error::UnclassifiedNativeFailure);
}

TEST(Ex2CIntegrationVulkanClassification, CoversEveryCurrentNativePhase)
{
    using Error = c::IntegrationErrorCode;
    using IntegrationPhase = c::IntegrationFailurePhase;
    using Native = vulkan::Ex2VulkanCNativePhase;
    using Status = c::IntegrationStatus;
    struct Case
    {
        Native native;
        Status status;
        IntegrationPhase phase;
        Error error;
    };
    const std::array cases{
        Case{Native::InstanceCreation, Status::Incomplete,
            IntegrationPhase::BackendInitialization,
            Error::BackendInitializationFailed},
        Case{Native::DeviceEnumeration, Status::Incomplete,
            IntegrationPhase::BackendInitialization,
            Error::BackendInitializationFailed},
        Case{Native::DeviceSelection, Status::Incomplete,
            IntegrationPhase::BackendInitialization,
            Error::BackendInitializationFailed},
        Case{Native::DeviceProperties, Status::Incomplete,
            IntegrationPhase::BackendInitialization,
            Error::BackendInitializationFailed},
        Case{Native::QueueSelection, Status::Incomplete,
            IntegrationPhase::BackendInitialization,
            Error::BackendInitializationFailed},
        Case{Native::LogicalDeviceCreation, Status::Incomplete,
            IntegrationPhase::BackendInitialization,
            Error::BackendInitializationFailed},
        Case{Native::ResourcePreflight, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::BufferCreation, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::MemoryAllocation, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::MemoryBinding, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::MemoryMapping, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::DescriptorCreation, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::PipelineConstruction, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::CommandCreation, Status::Incomplete,
            IntegrationPhase::ResourceAllocation,
            Error::ResourceAllocationFailed},
        Case{Native::TargetUpload, Status::Incomplete,
            IntegrationPhase::TargetUpload, Error::TargetUploadFailed},
        Case{Native::CounterReset, Status::Incomplete,
            IntegrationPhase::CounterReset, Error::CounterResetFailed},
        Case{Native::ResetCompletion, Status::Incomplete,
            IntegrationPhase::ResetCompletion, Error::ResetCompletionFailed},
        Case{Native::Submission, Status::SubmitFailed,
            IntegrationPhase::Submission, Error::SubmissionFailed},
        Case{Native::CompletionWait, Status::WaitFailed,
            IntegrationPhase::CompletionWait, Error::CompletionFailed},
        Case{Native::CounterReadback, Status::Incomplete,
            IntegrationPhase::CounterReadback, Error::CounterReadbackFailed},
        Case{Native::TargetDiagnosticReadback, Status::Incomplete,
            IntegrationPhase::TargetDiagnosticReadback,
            Error::TargetReadbackFailed}};

    for (const auto& test : cases)
    {
        ExpectFailure(c::ClassifyVulkanFailure(test.native, VK_ERROR_UNKNOWN),
            test.status, test.phase, test.error);
    }
    ExpectFailure(c::ClassifyVulkanFailure(
        static_cast<Native>(999), VK_ERROR_UNKNOWN),
        Status::Incomplete,
        IntegrationPhase::NativeFailure,
        Error::UnclassifiedNativeFailure);
}

TEST(Ex2CIntegrationVulkanClassification,
    TimeoutAndDeviceLossOverlayStatusWithoutErasingMappedPhase)
{
    using Error = c::IntegrationErrorCode;
    using Phase = c::IntegrationFailurePhase;
    using Native = vulkan::Ex2VulkanCNativePhase;
    using Status = c::IntegrationStatus;

    for (const auto [native, phase] : {
        std::pair{Native::CounterReset, Phase::CounterReset},
        std::pair{Native::CounterReadback, Phase::CounterReadback},
        std::pair{Native::CompletionWait, Phase::CompletionWait}})
    {
        ExpectFailure(c::ClassifyVulkanFailure(native, VK_TIMEOUT),
            Status::Timeout, phase, Error::OperationTimeout);
    }
    for (const auto [native, phase] : {
        std::pair{Native::TargetUpload, Phase::TargetUpload},
        std::pair{Native::Submission, Phase::Submission},
        std::pair{Native::CompletionWait, Phase::CompletionWait}})
    {
        ExpectFailure(c::ClassifyVulkanFailure(
            native, VK_ERROR_DEVICE_LOST),
            Status::DeviceLost, phase, Error::DeviceLost);
    }
}

TEST(Ex2CIntegrationProvenance,
    CurrentSelectedShaderHasMagicAndMatchesItsFile)
{
    vulkan::Ex2VulkanCOperation operation{
        ex2::ContentionConfiguration{1U, 1U, 1U},
        COMPUTELAB_EX2_C_SPIRV_PATH,
        0U};
    EXPECT_EQ(operation.LoadedShaderName(), "Ex2C.comp.spv");
    ASSERT_FALSE(operation.LoadedSpirv().empty());
    EXPECT_EQ(operation.LoadedSpirv().front(), 0x07230203U);
    EXPECT_EQ(
        ex2::Sha256(std::as_bytes(operation.LoadedSpirv())),
        ex2::Sha256File(COMPUTELAB_EX2_C_SPIRV_PATH));
}

TEST(Ex2CIntegrationCore, AllThreeApprovedCoreConditionsPassWithoutSkips)
{
    struct CoreCase
    {
        const char* label;
        std::uint64_t activeCounterCount;
        std::uint32_t expectedActiveValue;
    };
    constexpr std::array cases{
        CoreCase{"low", ex2::ContentionActiveAll, 1U},
        CoreCase{"medium", ex2::ContentionActiveOnePer32, 32U},
        CoreCase{"high", ex2::ContentionActive64, 16'384U}};

    for (const auto& test : cases)
    {
        SCOPED_TRACE(test.label);
        const auto observation = c::RunCrossBackendCorrectness(
            ex2::ContentionElementCount,
            test.activeCounterCount,
            0,
            0U,
            COMPUTELAB_EX2_C_SPIRV_PATH);

        EXPECT_TRUE(observation.Passed());
        EXPECT_TRUE(observation.physicalIdentityVerified);
        EXPECT_FALSE(c::IsZeroDeviceUuid(observation.verifiedDeviceUuid));
        EXPECT_EQ(observation.verifiedDeviceUuid,
            observation.vulkanDiagnostics.deviceUuid);
        EXPECT_TRUE(observation.vulkanShaderProvenanceVerified);
        EXPECT_EQ(observation.vulkanLoadedShaderName, "Ex2C.comp.spv");
        EXPECT_EQ(observation.vulkanLoadedSpirvSha256,
            observation.vulkanShaderFileSha256);
        EXPECT_EQ(observation.targetsSha256.size(), 64U);
        EXPECT_EQ(observation.expectedCountersSha256.size(), 64U);
        EXPECT_EQ(observation.targets.size(), ex2::ContentionElementCount);
        EXPECT_EQ(observation.expectedCounters.size(),
            ex2::ContentionElementCount);
        EXPECT_EQ(ex2::ClassifyCellEligibility(observation.configuration),
            ex2::CellEligibility::ApprovedCoreCell);

        for (const c::BackendObservation* backend : {
            &observation.cuda, &observation.vulkan})
        {
            EXPECT_TRUE(backend->nativeOperationCompleted);
            EXPECT_TRUE(backend->countersObserved);
            EXPECT_TRUE(backend->cpuComparisonPerformed);
            ASSERT_TRUE(backend->validationPassed.has_value());
            EXPECT_TRUE(*backend->validationPassed);
            EXPECT_EQ(backend->status, c::IntegrationStatus::Ok);
            EXPECT_TRUE(backend->safeForFurtherGpuCalls);
            EXPECT_TRUE(backend->nativeDispatchExecuted);
            EXPECT_EQ(backend->counters, observation.expectedCounters);
            EXPECT_EQ(backend->deviceTargets, observation.targets);
        }

        EXPECT_TRUE(observation.pairComparisonPerformed);
        EXPECT_TRUE(observation.pairCountersEqual);
        EXPECT_EQ(observation.cuda.counters, observation.vulkan.counters);
        VerifyLiteralOccupancy(observation.cuda.counters,
            test.activeCounterCount, test.expectedActiveValue);
        VerifyLiteralOccupancy(observation.vulkan.counters,
            test.activeCounterCount, test.expectedActiveValue);

        const std::string prefix = std::string{test.label} + "_";
        RecordProperty(
            (prefix + "uuid").c_str(), observation.verifiedDeviceUuidText);
        RecordProperty(
            (prefix + "targets_sha256").c_str(), observation.targetsSha256);
        RecordProperty((prefix + "expected_counters_sha256").c_str(),
            observation.expectedCountersSha256);
        RecordProperty((prefix + "spirv_sha256").c_str(),
            observation.vulkanLoadedSpirvSha256);
    }
}

} // namespace

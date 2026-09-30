#include "app/Ex2EIntegration.hpp"

#include "ex2/Ex2Configuration.hpp"
#include "ex2/Ex2HostTiming.hpp"
#include "ex2/Ex2Input.hpp"
#include "timing/HostTiming.hpp"

#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace
{

using computelab::ex2::HostTimingStatus;
using computelab::ex2::TransferDirection;
using namespace computelab::ex2::e;

[[nodiscard]] computelab::ex2::HostTimingIntervals SuccessfulHostTiming()
{
    using namespace std::chrono_literals;
    const computelab::timing::HostTimePoint t0{10ns};
    const computelab::timing::HostTimePoint t1{17ns};
    const computelab::timing::HostTimePoint t2{29ns};
    return computelab::ex2::CalculateHostTimingIntervals(
        HostTimingStatus::Ok, t0, t1, t2);
}

void ExpectSuccessfulBackend(
    const BackendObservation& backend,
    TransferDirection direction,
    std::uint64_t byteCount)
{
    EXPECT_TRUE(backend.nativeTransferCompleted);
    EXPECT_TRUE(backend.destinationObserved);
    EXPECT_TRUE(backend.cpuComparisonPerformed);
    ASSERT_TRUE(backend.validationPassed.has_value());
    EXPECT_TRUE(*backend.validationPassed);
    EXPECT_EQ(backend.status, IntegrationStatus::Ok);
    EXPECT_FALSE(backend.failurePhase.has_value());
    EXPECT_FALSE(backend.errorCode.has_value());
    EXPECT_TRUE(backend.safeForFurtherGpuCalls);
    EXPECT_EQ(backend.direction, direction);
    EXPECT_EQ(backend.expectedByteCount, byteCount);
    EXPECT_EQ(backend.expectedNativeCopyCount, byteCount == 0U ? 0U : 1U);
    ASSERT_TRUE(backend.completedNativeCopyCount.has_value());
    EXPECT_EQ(*backend.completedNativeCopyCount, byteCount == 0U ? 0U : 1U);
    ASSERT_TRUE(backend.copyExecuted.has_value());
    EXPECT_EQ(*backend.copyExecuted, byteCount != 0U);
    EXPECT_EQ(backend.destination.size(), byteCount);
    EXPECT_EQ(backend.hostTiming.status, HostTimingStatus::Ok);
    ASSERT_TRUE(backend.hostTiming.hostSubmissionNanoseconds.has_value());
    ASSERT_TRUE(backend.hostTiming.hostWaitNanoseconds.has_value());
    ASSERT_TRUE(backend.hostTiming.hostCompletionNanoseconds.has_value());
    EXPECT_EQ(
        *backend.hostTiming.hostSubmissionNanoseconds
            + *backend.hostTiming.hostWaitNanoseconds,
        *backend.hostTiming.hostCompletionNanoseconds);
    EXPECT_TRUE(backend.hostCaptureComplete);
}

void ExpectSuccessfulPair(
    const CrossBackendObservation& observation,
    TransferDirection direction,
    std::uint64_t byteCount)
{
    EXPECT_TRUE(observation.physicalIdentityVerified);
    EXPECT_FALSE(IsZeroDeviceUuid(observation.verifiedDeviceUuid));
    EXPECT_EQ(observation.verifiedDeviceUuidText.size(), 36U);
    EXPECT_EQ(observation.verifiedDeviceUuidText,
        FormatDeviceUuid(observation.verifiedDeviceUuid));
    EXPECT_EQ(observation.vulkanDiagnostics.deviceUuid, observation.verifiedDeviceUuid);
    EXPECT_EQ(observation.source.size(), byteCount);
    EXPECT_EQ(observation.expectedDestination.size(), byteCount);
    EXPECT_FALSE(observation.sourceSha256.empty());
    EXPECT_FALSE(observation.expectedDestinationSha256.empty());
    ExpectSuccessfulBackend(observation.cuda, direction, byteCount);
    ExpectSuccessfulBackend(observation.vulkan, direction, byteCount);
    EXPECT_TRUE(observation.cuda.hModeUninstrumented);
    EXPECT_TRUE(observation.vulkan.hModeUninstrumented);
    EXPECT_FALSE(observation.vulkanDiagnostics.timestampQueryPoolCreated);
    EXPECT_EQ(observation.vulkanDiagnostics.preparedCommandRecordCount, 1U);
    EXPECT_EQ(
        observation.vulkanDiagnostics.preparedNativeCopyCount,
        byteCount == 0U ? 0U : 1U);
    EXPECT_EQ(observation.cuda.destination, observation.expectedDestination);
    EXPECT_EQ(observation.vulkan.destination, observation.expectedDestination);
    EXPECT_TRUE(observation.pairComparisonPerformed);
    EXPECT_TRUE(observation.pairDestinationsEqual);
    EXPECT_EQ(observation.vulkanDiagnostics.validationErrorCount, 0U);
    EXPECT_TRUE(observation.Passed());
}

[[nodiscard]] DeviceUuid SyntheticDeviceUuid()
{
    DeviceUuid uuid{};
    for (std::size_t index = 0; index < uuid.size(); ++index)
        uuid[index] = static_cast<std::uint8_t>(index + 1U);
    return uuid;
}

[[nodiscard]] computelab::vulkan::Ex2VulkanEDiagnostics
AcceptedVulkanDiagnostics(
    const DeviceUuid& uuid,
    std::uint64_t byteCount,
    bool hMode)
{
    computelab::vulkan::Ex2VulkanEDiagnostics diagnostics;
    diagnostics.deviceUuid = uuid;
    diagnostics.queueFamily.queueFlags = VK_QUEUE_COMPUTE_BIT;
    diagnostics.synchronization2Enabled = true;
    diagnostics.timestampQueryPoolCreated = !hMode && byteCount != 0U;
    diagnostics.uploadMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    diagnostics.deviceMemoryFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    diagnostics.readbackMemoryFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    diagnostics.uploadAllocationBytes = byteCount;
    diagnostics.deviceAllocationBytes = byteCount;
    diagnostics.readbackAllocationBytes = byteCount;
    diagnostics.preparedCommandRecordCount = 1U;
    diagnostics.preparedNativeCopyCount = byteCount == 0U ? 0U : 1U;
    return diagnostics;
}

[[nodiscard]] BackendObservation AcceptedBackendObservation(
    TransferDirection direction,
    std::span<const std::uint8_t> destination,
    bool hMode)
{
    BackendObservation observation;
    observation.nativeTransferCompleted = true;
    observation.destinationObserved = true;
    observation.cpuComparisonPerformed = true;
    observation.validationPassed = true;
    observation.status = IntegrationStatus::Ok;
    observation.safeForFurtherGpuCalls = true;
    observation.direction = direction;
    observation.expectedByteCount = destination.size();
    observation.expectedNativeCopyCount = destination.empty() ? 0U : 1U;
    observation.completedNativeCopyCount = observation.expectedNativeCopyCount;
    observation.copyExecuted = !destination.empty();
    observation.hModeUninstrumented = hMode;
    observation.destination.assign(destination.begin(), destination.end());
    observation.hostTiming = SuccessfulHostTiming();
    observation.hostCaptureComplete = true;
    return observation;
}

[[nodiscard]] CrossBackendObservation AcceptedCrossBackendObservation()
{
    constexpr std::uint64_t ByteCount = 4U;
    CrossBackendObservation observation;
    observation.configuration = computelab::ex2::MakeConfiguration(
        computelab::ex2::TransferConfiguration{
            computelab::ex2::TransferVariant::E1,
            ByteCount,
            TransferDirection::HostToDevice});
    observation.verifiedDeviceUuid = SyntheticDeviceUuid();
    observation.verifiedDeviceUuidText = FormatDeviceUuid(
        observation.verifiedDeviceUuid);
    observation.physicalIdentityVerified = true;
    observation.source = computelab::ex2::GenerateByteInput(
        computelab::ex2::CoreInputSeed, ByteCount);
    observation.expectedDestination = observation.source;
    observation.sourceSha256 = "source-sha256";
    observation.expectedDestinationSha256 = "expected-sha256";
    observation.cuda = AcceptedBackendObservation(
        TransferDirection::HostToDevice,
        observation.expectedDestination,
        true);
    observation.vulkan = AcceptedBackendObservation(
        TransferDirection::HostToDevice,
        observation.expectedDestination,
        true);
    observation.pairComparisonPerformed = true;
    observation.pairDestinationsEqual = true;
    observation.vulkanDiagnostics = AcceptedVulkanDiagnostics(
        observation.verifiedDeviceUuid, ByteCount, true);
    return observation;
}

[[nodiscard]] NativeInstrumentationSmokeObservation
AcceptedNativeSmokeObservation(std::uint64_t byteCount)
{
    NativeInstrumentationSmokeObservation observation;
    observation.configuration = computelab::ex2::MakeConfiguration(
        computelab::ex2::TransferConfiguration{
            computelab::ex2::TransferVariant::E1,
            byteCount,
            TransferDirection::HostToDevice});
    observation.verifiedDeviceUuid = SyntheticDeviceUuid();
    observation.verifiedDeviceUuidText = FormatDeviceUuid(
        observation.verifiedDeviceUuid);
    observation.physicalIdentityVerified = true;
    const auto destination = computelab::ex2::GenerateByteInput(
        computelab::ex2::CoreInputSeed, byteCount);
    observation.cuda.transfer = AcceptedBackendObservation(
        TransferDirection::HostToDevice, destination, false);
    observation.vulkan.transfer = AcceptedBackendObservation(
        TransferDirection::HostToDevice, destination, false);
    const auto timingStatus = byteCount == 0U
        ? NativeTimingSmokeStatus::NotApplicable
        : NativeTimingSmokeStatus::Valid;
    observation.cuda.nativeTimingStatus = timingStatus;
    observation.vulkan.nativeTimingStatus = timingStatus;
    observation.cuda.nativeTimingRetrieved = true;
    observation.vulkan.nativeTimingRetrieved = true;
    observation.cuda.nativeIntervalPresent = byteCount != 0U;
    observation.vulkan.nativeIntervalPresent = byteCount != 0U;
    observation.cuda.nativeTimingMetadataValid = true;
    observation.vulkan.nativeTimingMetadataValid = true;
    observation.cuda.nativeTimingMethod = "cudaEventElapsedTime";
    observation.vulkan.nativeTimingMethod =
        "vkCmdWriteTimestamp2/vkGetQueryPoolResults";
    observation.pairComparisonPerformed = true;
    observation.pairDestinationsEqual = true;
    observation.vulkanDiagnostics = AcceptedVulkanDiagnostics(
        observation.verifiedDeviceUuid, byteCount, false);
    return observation;
}

TEST(Ex2EIntegrationPure, FormatsCanonicalDeviceUuidFixture)
{
    const DeviceUuid uuid{
        0x00U, 0x11U, 0x22U, 0x33U, 0x44U, 0x55U, 0x66U, 0x77U,
        0x88U, 0x99U, 0xaaU, 0xbbU, 0xccU, 0xddU, 0xeeU, 0xffU};
    constexpr std::string_view expected =
        "00112233-4455-6677-8899-aabbccddeeff";
    const auto formatted = FormatDeviceUuid(uuid);
    EXPECT_EQ(formatted, expected);
    ASSERT_EQ(formatted.size(), 36U);
    for (const std::size_t offset : {8U, 13U, 18U, 23U})
        EXPECT_EQ(formatted[offset], '-');
    EXPECT_EQ(VerifySamePhysicalDevice(uuid, uuid), expected);
}

TEST(Ex2EIntegrationPure, RejectsZeroAndMismatchedDeviceUuids)
{
    DeviceUuid zero{};
    DeviceUuid first{};
    first[0] = 1U;
    DeviceUuid second = first;
    EXPECT_TRUE(IsZeroDeviceUuid(zero));
    EXPECT_FALSE(IsZeroDeviceUuid(first));
    EXPECT_THROW(static_cast<void>(FormatDeviceUuid(zero)), std::invalid_argument);
    EXPECT_EQ(VerifySamePhysicalDevice(first, second), FormatDeviceUuid(first));
    second[15] = 2U;
    EXPECT_THROW(static_cast<void>(VerifySamePhysicalDevice(zero, first)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(VerifySamePhysicalDevice(first, zero)), std::runtime_error);
    EXPECT_THROW(static_cast<void>(VerifySamePhysicalDevice(first, second)), std::runtime_error);
}

TEST(Ex2EIntegrationPure, ValidatesTheEntireDeclaredSource)
{
    const auto source = computelab::ex2::GenerateByteInput(
        computelab::ex2::CoreInputSeed, 257U);
    const auto configuration = computelab::ex2::MakeConfiguration(
        computelab::ex2::TransferConfiguration{
            computelab::ex2::TransferVariant::E1,
            source.size(),
            TransferDirection::HostToDevice});
    EXPECT_NO_THROW(ValidateDeclaredSource(configuration, source));
    auto corrupted = source;
    corrupted.back() ^= 1U;
    EXPECT_THROW(ValidateDeclaredSource(configuration, corrupted), std::invalid_argument);
}

TEST(Ex2EIntegrationE3, RequiresExactCaptureAndPostCompletionOrder)
{
    constexpr std::array correct{
        HostCaptureStep::Prepared,
        HostCaptureStep::CaptureT0,
        HostCaptureStep::Submit,
        HostCaptureStep::CaptureT1,
        HostCaptureStep::Wait,
        HostCaptureStep::CaptureT2,
        HostCaptureStep::PostCompletion};
    EXPECT_TRUE(IsValidHostCaptureOrder(correct));
    auto readbackBeforeT2 = correct;
    std::swap(readbackBeforeT2[5], readbackBeforeT2[6]);
    EXPECT_FALSE(IsValidHostCaptureOrder(readbackBeforeT2));
    EXPECT_FALSE(IsValidHostCaptureOrder(
        std::span<const HostCaptureStep>{correct}.first(6U)));
}

TEST(Ex2EIntegrationE3, PreservesTruthfulSuccessAndFailureIntervals)
{
    using namespace std::chrono_literals;
    const computelab::timing::HostTimePoint t0{10ns};
    const computelab::timing::HostTimePoint t1{17ns};
    const computelab::timing::HostTimePoint t2{29ns};
    const auto success = computelab::ex2::CalculateHostTimingIntervals(
        HostTimingStatus::Ok, t0, t1, t2);
    EXPECT_EQ(success.hostSubmissionNanoseconds, 7U);
    EXPECT_EQ(success.hostWaitNanoseconds, 12U);
    EXPECT_EQ(success.hostCompletionNanoseconds, 19U);

    const auto submitFailed = computelab::ex2::CalculateHostTimingIntervals(
        HostTimingStatus::SubmitFailed, t0, std::nullopt, std::nullopt);
    EXPECT_EQ(submitFailed.status, HostTimingStatus::SubmitFailed);
    EXPECT_FALSE(submitFailed.hostSubmissionNanoseconds.has_value());
    EXPECT_FALSE(submitFailed.hostWaitNanoseconds.has_value());
    EXPECT_FALSE(submitFailed.hostCompletionNanoseconds.has_value());

    for (const auto status : {HostTimingStatus::WaitFailed, HostTimingStatus::Timeout})
    {
        const auto failed = computelab::ex2::CalculateHostTimingIntervals(
            status, t0, t1, std::nullopt);
        EXPECT_EQ(failed.status, status);
        EXPECT_EQ(failed.hostSubmissionNanoseconds, 7U);
        EXPECT_FALSE(failed.hostWaitNanoseconds.has_value());
        EXPECT_FALSE(failed.hostCompletionNanoseconds.has_value());
    }
    const auto reversed = computelab::ex2::CalculateHostTimingIntervals(
        HostTimingStatus::Ok, t1, t0, t2);
    EXPECT_EQ(reversed.status, HostTimingStatus::TimestampInvalid);
}

TEST(Ex2EIntegrationPure, CompletedValidationRequiresEveryInvariant)
{
    const std::array<std::uint8_t, 4U> source{0x9DU, 0x1AU, 0xA2U, 0xF8U};
    const auto timing = SuccessfulHostTiming();
    const auto valid = ValidateCompletedResult(
        source, source, source, source, 1U, 1U, true, true, true, timing);
    EXPECT_TRUE(valid.Passed());

    auto invalidTiming = timing;
    invalidTiming.hostCompletionNanoseconds.reset();
    EXPECT_FALSE(ValidateCompletedResult(
        source, source, source, source, 1U, 1U, true, true, true, invalidTiming).Passed());
    EXPECT_FALSE(ValidateCompletedResult(
        source, source, source, source, 1U, 0U, true, true, true, timing).Passed());
}

TEST(Ex2EIntegrationPure, InterruptedAndUnsafeObservationsCannotPass)
{
    const auto safe = MakeInterruptedObservation(
        TransferDirection::HostToDevice, 257U, true);
    const auto unsafe = MakeInterruptedObservation(
        TransferDirection::HostToDevice, 257U, false);
    EXPECT_EQ(safe.failurePhase, IntegrationFailurePhase::InterruptedSecondBackend);
    EXPECT_EQ(
        ClassifyFailedSessionResourceDisposition(safe),
        FailedSessionResourceDisposition::DestroyNormally);
    EXPECT_EQ(
        ClassifyFailedSessionResourceDisposition(unsafe),
        FailedSessionResourceDisposition::PreserveForProcessTeardown);
    EXPECT_NE(safe.status, IntegrationStatus::Ok);
    EXPECT_NE(unsafe.status, IntegrationStatus::Ok);
}

TEST(Ex2EIntegrationAcceptance, StatusAndComparisonFlagsAloneCannotPass)
{
    CrossBackendObservation cross;
    cross.physicalIdentityVerified = true;
    cross.cuda.status = IntegrationStatus::Ok;
    cross.vulkan.status = IntegrationStatus::Ok;
    cross.cuda.validationPassed = true;
    cross.vulkan.validationPassed = true;
    cross.pairComparisonPerformed = true;
    cross.pairDestinationsEqual = true;
    EXPECT_FALSE(cross.Passed());

    NativeInstrumentationSmokeObservation smoke;
    smoke.physicalIdentityVerified = true;
    smoke.cuda.transfer.status = IntegrationStatus::Ok;
    smoke.vulkan.transfer.status = IntegrationStatus::Ok;
    smoke.cuda.transfer.validationPassed = true;
    smoke.vulkan.transfer.validationPassed = true;
    smoke.cuda.nativeTimingStatus = NativeTimingSmokeStatus::Valid;
    smoke.vulkan.nativeTimingStatus = NativeTimingSmokeStatus::Valid;
    smoke.pairComparisonPerformed = true;
    smoke.pairDestinationsEqual = true;
    EXPECT_FALSE(smoke.Passed());
}

TEST(Ex2EIntegrationAcceptance, CrossBackendPredicateRejectsEachCorruptedInvariant)
{
    const auto accepted = AcceptedCrossBackendObservation();
    ASSERT_TRUE(accepted.Passed());
    const auto expectRejected = [&](std::string_view label, auto corrupt) {
        SCOPED_TRACE(label);
        auto observation = accepted;
        corrupt(observation);
        EXPECT_FALSE(observation.Passed());
    };

    expectRejected("seed", [](auto& value) { ++value.configuration.common.seed; });
    expectRejected("generator", [](auto& value) { value.configuration.common.generatorRevision = "wrong"; });
    expectRejected("execution mode", [](auto& value) { value.configuration.common.executionMode = computelab::ex2::LogicalExecutionMode::Ordinary; });
    expectRejected("boundary", [](auto& value) { value.configuration.common.operationBoundary = computelab::ex2::OperationBoundary::SingleDispatchCompletion; });
    expectRejected("parameter family", [](auto& value) { value.configuration.parameters = computelab::ex2::LinearConfiguration{}; });
    expectRejected("variant direction", [](auto& value) { std::get<computelab::ex2::TransferConfiguration>(value.configuration.parameters).variant = computelab::ex2::TransferVariant::E2; });
    expectRejected("physical identity", [](auto& value) { value.physicalIdentityVerified = false; });
    expectRejected("zero uuid", [](auto& value) { value.verifiedDeviceUuid = {}; value.vulkanDiagnostics.deviceUuid = {}; });
    expectRejected("diagnostic uuid", [](auto& value) { ++value.vulkanDiagnostics.deviceUuid[0]; });
    expectRejected("source hash", [](auto& value) { value.sourceSha256.clear(); });
    expectRejected("expected hash", [](auto& value) { value.expectedDestinationSha256.clear(); });
    expectRejected("declared source length", [](auto& value) { value.source.pop_back(); });
    expectRejected("declared source byte", [](auto& value) { value.source.back() ^= 1U; });
    expectRejected("cpu expectation", [](auto& value) { value.expectedDestination.back() ^= 1U; });
    expectRejected("cuda completion", [](auto& value) { value.cuda.nativeTransferCompleted = false; });
    expectRejected("vulkan completion", [](auto& value) { value.vulkan.nativeTransferCompleted = false; });
    expectRejected("cuda destination observation", [](auto& value) { value.cuda.destinationObserved = false; });
    expectRejected("vulkan destination observation", [](auto& value) { value.vulkan.destinationObserved = false; });
    expectRejected("cuda cpu comparison", [](auto& value) { value.cuda.cpuComparisonPerformed = false; });
    expectRejected("vulkan cpu comparison", [](auto& value) { value.vulkan.cpuComparisonPerformed = false; });
    expectRejected("cuda validation", [](auto& value) { value.cuda.validationPassed = false; });
    expectRejected("vulkan validation", [](auto& value) { value.vulkan.validationPassed.reset(); });
    expectRejected("cuda status", [](auto& value) { value.cuda.status = IntegrationStatus::Incomplete; });
    expectRejected("vulkan status", [](auto& value) { value.vulkan.status = IntegrationStatus::ValidationFailed; });
    expectRejected("cuda failure phase", [](auto& value) { value.cuda.failurePhase = IntegrationFailurePhase::ValidationMismatch; });
    expectRejected("vulkan error code", [](auto& value) { value.vulkan.errorCode = IntegrationErrorCode::DestinationMismatch; });
    expectRejected("native diagnostic", [](auto& value) { value.cuda.nativeErrorCode = 1; });
    expectRejected("unsafe session", [](auto& value) { value.vulkan.safeForFurtherGpuCalls = false; });
    expectRejected("direction", [](auto& value) { value.cuda.direction = TransferDirection::DeviceToHost; });
    expectRejected("declared byte count", [](auto& value) { ++value.vulkan.expectedByteCount; });
    expectRejected("expected copy count", [](auto& value) { value.cuda.expectedNativeCopyCount = 0U; });
    expectRejected("completed copy count", [](auto& value) { value.vulkan.completedNativeCopyCount = 0U; });
    expectRejected("copy execution", [](auto& value) { value.cuda.copyExecuted = false; });
    expectRejected("cuda H truth", [](auto& value) { value.cuda.hModeUninstrumented = false; });
    expectRejected("vulkan H truth", [](auto& value) { value.vulkan.hModeUninstrumented = false; });
    expectRejected("cuda capture", [](auto& value) { value.cuda.hostCaptureComplete = false; });
    expectRejected("vulkan capture", [](auto& value) { value.vulkan.hostCaptureComplete = false; });
    expectRejected("host status", [](auto& value) { value.cuda.hostTiming.status = HostTimingStatus::Incomplete; });
    expectRejected("host decomposition", [](auto& value) { ++*value.vulkan.hostTiming.hostCompletionNanoseconds; });
    expectRejected("cuda destination length", [](auto& value) { value.cuda.destination.pop_back(); });
    expectRejected("vulkan destination bytes", [](auto& value) { value.vulkan.destination.back() ^= 1U; });
    expectRejected("pair comparison", [](auto& value) { value.pairComparisonPerformed = false; });
    expectRejected("pair equality", [](auto& value) { value.pairDestinationsEqual = false; });
    expectRejected("host visibility", [](auto& value) { value.vulkanDiagnostics.uploadMemoryFlags = 0U; });
    expectRejected("device locality", [](auto& value) { value.vulkanDiagnostics.deviceMemoryFlags = 0U; });
    expectRejected("allocation size", [](auto& value) { value.vulkanDiagnostics.readbackAllocationBytes = 3U; });
    expectRejected("compute queue", [](auto& value) { value.vulkanDiagnostics.queueFamily.queueFlags = 0U; });
    expectRejected("synchronization2", [](auto& value) { value.vulkanDiagnostics.synchronization2Enabled = false; });
    expectRejected("prepared command", [](auto& value) { value.vulkanDiagnostics.preparedCommandRecordCount = 0U; });
    expectRejected("prepared copy", [](auto& value) { value.vulkanDiagnostics.preparedNativeCopyCount = 0U; });
    expectRejected("H query pool", [](auto& value) { value.vulkanDiagnostics.timestampQueryPoolCreated = true; });
    expectRejected("validation errors", [](auto& value) { value.vulkanDiagnostics.validationErrorCount = 1U; });
}

TEST(Ex2EIntegrationAcceptance, NativeSmokePredicateRejectsEachCorruptedInvariant)
{
    const auto accepted = AcceptedNativeSmokeObservation(4U);
    ASSERT_TRUE(accepted.Passed());
    const auto expectRejected = [&](std::string_view label, auto corrupt) {
        SCOPED_TRACE(label);
        auto observation = accepted;
        corrupt(observation);
        EXPECT_FALSE(observation.Passed());
    };

    expectRejected("configuration", [](auto& value) { value.configuration.common.seed = 0U; });
    expectRejected("identity", [](auto& value) { value.physicalIdentityVerified = false; });
    expectRejected("zero uuid", [](auto& value) { value.verifiedDeviceUuid = {}; value.vulkanDiagnostics.deviceUuid = {}; });
    expectRejected("diagnostic uuid", [](auto& value) { ++value.vulkanDiagnostics.deviceUuid[0]; });
    expectRejected("cuda completion", [](auto& value) { value.cuda.transfer.nativeTransferCompleted = false; });
    expectRejected("vulkan completion", [](auto& value) { value.vulkan.transfer.nativeTransferCompleted = false; });
    expectRejected("cuda destination", [](auto& value) { value.cuda.transfer.destinationObserved = false; });
    expectRejected("vulkan destination", [](auto& value) { value.vulkan.transfer.destinationObserved = false; });
    expectRejected("cuda cpu comparison", [](auto& value) { value.cuda.transfer.cpuComparisonPerformed = false; });
    expectRejected("vulkan cpu comparison", [](auto& value) { value.vulkan.transfer.cpuComparisonPerformed = false; });
    expectRejected("cuda validation", [](auto& value) { value.cuda.transfer.validationPassed = false; });
    expectRejected("vulkan validation", [](auto& value) { value.vulkan.transfer.validationPassed.reset(); });
    expectRejected("cuda status", [](auto& value) { value.cuda.transfer.status = IntegrationStatus::Incomplete; });
    expectRejected("vulkan failure", [](auto& value) { value.vulkan.transfer.failurePhase = IntegrationFailurePhase::NativeFailure; });
    expectRejected("direction", [](auto& value) { value.cuda.transfer.direction = TransferDirection::DeviceToHost; });
    expectRejected("byte count", [](auto& value) { ++value.vulkan.transfer.expectedByteCount; });
    expectRejected("expected copies", [](auto& value) { value.cuda.transfer.expectedNativeCopyCount = 0U; });
    expectRejected("completed copies", [](auto& value) { value.vulkan.transfer.completedNativeCopyCount = 0U; });
    expectRejected("executed copy", [](auto& value) { value.cuda.transfer.copyExecuted = false; });
    expectRejected("N labeled H", [](auto& value) { value.vulkan.transfer.hModeUninstrumented = true; });
    expectRejected("cuda capture", [](auto& value) { value.cuda.transfer.hostCaptureComplete = false; });
    expectRejected("vulkan timing", [](auto& value) { value.vulkan.transfer.hostTiming.status = HostTimingStatus::Incomplete; });
    expectRejected("host decomposition", [](auto& value) { ++*value.cuda.transfer.hostTiming.hostCompletionNanoseconds; });
    expectRejected("cuda N status", [](auto& value) { value.cuda.nativeTimingStatus = NativeTimingSmokeStatus::NotApplicable; });
    expectRejected("vulkan N status", [](auto& value) { value.vulkan.nativeTimingStatus = NativeTimingSmokeStatus::Failed; });
    expectRejected("cuda retrieval", [](auto& value) { value.cuda.nativeTimingRetrieved = false; });
    expectRejected("vulkan retrieval", [](auto& value) { value.vulkan.nativeTimingRetrieved = false; });
    expectRejected("cuda metadata", [](auto& value) { value.cuda.nativeTimingMetadataValid = false; });
    expectRejected("vulkan metadata", [](auto& value) { value.vulkan.nativeTimingMetadataValid = false; });
    expectRejected("cuda method", [](auto& value) { value.cuda.nativeTimingMethod = "wrong"; });
    expectRejected("vulkan method", [](auto& value) { value.vulkan.nativeTimingMethod = "wrong"; });
    expectRejected("cuda interval", [](auto& value) { value.cuda.nativeIntervalPresent = false; });
    expectRejected("vulkan interval", [](auto& value) { value.vulkan.nativeIntervalPresent = false; });
    expectRejected("destination length", [](auto& value) { value.cuda.transfer.destination.pop_back(); });
    expectRejected("destination equality", [](auto& value) { value.vulkan.transfer.destination.back() ^= 1U; });
    expectRejected("pair comparison", [](auto& value) { value.pairComparisonPerformed = false; });
    expectRejected("pair equality", [](auto& value) { value.pairDestinationsEqual = false; });
    expectRejected("query pool", [](auto& value) { value.vulkanDiagnostics.timestampQueryPoolCreated = false; });
    expectRejected("prepared command", [](auto& value) { value.vulkanDiagnostics.preparedCommandRecordCount = 0U; });
    expectRejected("validation errors", [](auto& value) { value.vulkanDiagnostics.validationErrorCount = 1U; });
}

TEST(Ex2EIntegrationAcceptance, ZeroByteNativeSmokeRequiresNotApplicableWithoutDuration)
{
    const auto accepted = AcceptedNativeSmokeObservation(0U);
    ASSERT_TRUE(accepted.Passed());

    auto cudaDuration = accepted;
    cudaDuration.cuda.nativeIntervalPresent = true;
    EXPECT_FALSE(cudaDuration.Passed());

    auto vulkanDuration = accepted;
    vulkanDuration.vulkan.nativeIntervalPresent = true;
    EXPECT_FALSE(vulkanDuration.Passed());

    auto cudaStatus = accepted;
    cudaStatus.cuda.nativeTimingStatus = NativeTimingSmokeStatus::Valid;
    EXPECT_FALSE(cudaStatus.Passed());

    auto vulkanStatus = accepted;
    vulkanStatus.vulkan.nativeTimingStatus = NativeTimingSmokeStatus::Valid;
    EXPECT_FALSE(vulkanStatus.Passed());

    auto inventedCopy = accepted;
    inventedCopy.vulkan.transfer.completedNativeCopyCount = 1U;
    EXPECT_FALSE(inventedCopy.Passed());
}

TEST(Ex2EIntegrationClassifier, MapsEveryCudaNativePhase)
{
    using Phase = computelab::cuda::Ex2CudaENativePhase;
    constexpr std::array phases{
        Phase::DeviceSelection,
        Phase::RuntimeInitialization,
        Phase::DeviceProperties,
        Phase::ResourcePreflight,
        Phase::StreamCreation,
        Phase::PinnedSourceAllocation,
        Phase::PinnedDestinationAllocation,
        Phase::DeviceAllocation,
        Phase::HostSourcePreparation,
        Phase::DeviceSourcePreparation,
        Phase::PreparationCompletion,
        Phase::StartMarker,
        Phase::TransferSubmission,
        Phase::StopMarker,
        Phase::CompletionWait,
        Phase::ValidationReadback,
        Phase::ValidationReadbackCompletion,
        Phase::NativeTimingRetrieval,
        Phase::NativeTimingConversion};
    for (const auto phase : phases)
    {
        const auto classification = ClassifyCudaFailure(phase, 700);
        EXPECT_EQ(classification.nativeErrorCode, 700);
        EXPECT_NE(classification.status, IntegrationStatus::Ok);
        EXPECT_NE(classification.phase, IntegrationFailurePhase::NativeFailure);
        EXPECT_NE(
            classification.errorCode,
            IntegrationErrorCode::UnclassifiedNativeFailure);
    }
    EXPECT_EQ(
        ClassifyCudaFailure(Phase::TransferSubmission, 700).status,
        IntegrationStatus::SubmitFailed);
    EXPECT_EQ(
        ClassifyCudaFailure(Phase::CompletionWait, 700).status,
        IntegrationStatus::WaitFailed);
    EXPECT_FALSE(ClassifyCudaFailure(Phase::DeviceSourcePreparation, 700)
        .safeForFurtherGpuCalls);
}

TEST(Ex2EIntegrationClassifier, MapsEveryVulkanNativePhase)
{
    using Phase = computelab::vulkan::Ex2VulkanENativePhase;
    constexpr std::array phases{
        Phase::InstanceCreation,
        Phase::DeviceEnumeration,
        Phase::DeviceSelection,
        Phase::DeviceProperties,
        Phase::QueueSelection,
        Phase::LogicalDeviceCreation,
        Phase::ResourcePreflight,
        Phase::BufferCreation,
        Phase::MemoryAllocation,
        Phase::MemoryBinding,
        Phase::MemoryMapping,
        Phase::HostSourcePreparation,
        Phase::NoncoherentFlush,
        Phase::DeviceSourcePreparation,
        Phase::PreparationCompletion,
        Phase::CommandCreation,
        Phase::CommandRecording,
        Phase::QueryReset,
        Phase::Submission,
        Phase::CompletionWait,
        Phase::ValidationReadback,
        Phase::HostReadVisibility,
        Phase::NoncoherentInvalidate,
        Phase::NativeTimingRetrieval,
        Phase::NativeTimingConversion};
    for (const auto phase : phases)
    {
        const auto classification = ClassifyVulkanFailure(phase, VK_ERROR_UNKNOWN);
        EXPECT_EQ(
            classification.nativeErrorCode,
            static_cast<std::int64_t>(VK_ERROR_UNKNOWN));
        EXPECT_NE(classification.status, IntegrationStatus::Ok);
        EXPECT_NE(classification.phase, IntegrationFailurePhase::NativeFailure);
        EXPECT_NE(
            classification.errorCode,
            IntegrationErrorCode::UnclassifiedNativeFailure);
    }
}

TEST(Ex2EIntegrationClassifier, PreservesVulkanPhaseWhenOverlayingTimeoutOrDeviceLost)
{
    using Phase = computelab::vulkan::Ex2VulkanENativePhase;
    for (const auto phase : {
        Phase::DeviceSourcePreparation,
        Phase::ValidationReadback,
        Phase::CompletionWait})
    {
        const auto baseline = ClassifyVulkanFailure(phase, VK_ERROR_UNKNOWN);
        const auto timeout = ClassifyVulkanFailure(phase, VK_TIMEOUT);
        const auto lost = ClassifyVulkanFailure(phase, VK_ERROR_DEVICE_LOST);
        EXPECT_EQ(timeout.phase, baseline.phase);
        EXPECT_EQ(timeout.status, IntegrationStatus::Timeout);
        EXPECT_EQ(timeout.errorCode, IntegrationErrorCode::OperationTimeout);
        EXPECT_FALSE(timeout.safeForFurtherGpuCalls);
        EXPECT_EQ(lost.phase, baseline.phase);
        EXPECT_EQ(lost.status, IntegrationStatus::DeviceLost);
        EXPECT_EQ(lost.errorCode, IntegrationErrorCode::DeviceLost);
        EXPECT_FALSE(lost.safeForFurtherGpuCalls);
    }
}

TEST(Ex2EIntegrationClassifier, BoundsUnknownNativePhases)
{
    const auto cuda = ClassifyCudaFailure(
        static_cast<computelab::cuda::Ex2CudaENativePhase>(-1), 999);
    const auto vulkan = ClassifyVulkanFailure(
        static_cast<computelab::vulkan::Ex2VulkanENativePhase>(-1),
        VK_ERROR_UNKNOWN);
    EXPECT_EQ(cuda.phase, IntegrationFailurePhase::NativeFailure);
    EXPECT_EQ(cuda.errorCode, IntegrationErrorCode::UnclassifiedNativeFailure);
    EXPECT_EQ(vulkan.phase, IntegrationFailurePhase::NativeFailure);
    EXPECT_EQ(vulkan.errorCode, IntegrationErrorCode::UnclassifiedNativeFailure);
    EXPECT_NE(cuda.status, IntegrationStatus::Ok);
    EXPECT_NE(vulkan.status, IntegrationStatus::Ok);
}

class Ex2EIntegrationCore
    : public testing::TestWithParam<std::tuple<TransferDirection, std::uint64_t>>
{};

std::string CoreCaseName(
    const testing::TestParamInfo<Ex2EIntegrationCore::ParamType>& info)
{
    const auto direction = std::get<0>(info.param);
    const auto byteCount = std::get<1>(info.param);
    return std::string{
        direction == TransferDirection::HostToDevice ? "E1_" : "E2_"}
        + std::to_string(byteCount);
}

TEST_P(Ex2EIntegrationCore, ExecutesRealSameGpuPair)
{
    const auto [direction, byteCount] = GetParam();
    const auto observation = RunCrossBackendCorrectness(direction, byteCount);
    ExpectSuccessfulPair(observation, direction, byteCount);
    RecordProperty("device_uuid", observation.verifiedDeviceUuidText);
    RecordProperty("source_length", std::to_string(observation.source.size()));
    RecordProperty("cuda_destination_length", std::to_string(observation.cuda.destination.size()));
    RecordProperty("vulkan_destination_length", std::to_string(observation.vulkan.destination.size()));
    RecordProperty("cuda_copy_count", std::to_string(*observation.cuda.completedNativeCopyCount));
    RecordProperty("vulkan_copy_count", std::to_string(*observation.vulkan.completedNativeCopyCount));
    RecordProperty("cuda_e3_status", "Ok");
    RecordProperty("vulkan_e3_status", "Ok");
    RecordProperty("full_cpu_cuda_vulkan_equality", "true");
}

INSTANTIATE_TEST_SUITE_P(
    SixApprovedCells,
    Ex2EIntegrationCore,
    testing::Values(
        std::tuple{TransferDirection::HostToDevice, 1'024ULL},
        std::tuple{TransferDirection::HostToDevice, 1'048'576ULL},
        std::tuple{TransferDirection::HostToDevice, 67'108'864ULL},
        std::tuple{TransferDirection::DeviceToHost, 1'024ULL},
        std::tuple{TransferDirection::DeviceToHost, 1'048'576ULL},
        std::tuple{TransferDirection::DeviceToHost, 67'108'864ULL}),
    CoreCaseName);

TEST(Ex2EIntegrationReal, IndependentLiteralFixturePassesBothDirections)
{
    const std::array<std::uint8_t, 4U> literal{0x9DU, 0x1AU, 0xA2U, 0xF8U};
    for (const auto direction : {
        TransferDirection::HostToDevice,
        TransferDirection::DeviceToHost})
    {
        const auto observation = RunCrossBackendCorrectnessWithDeclaredData(
            direction, literal, literal);
        ExpectSuccessfulPair(observation, direction, literal.size());
    }
}

TEST(Ex2EIntegrationReal, ZeroOneAnd257PassBothDirections)
{
    for (const auto direction : {
        TransferDirection::HostToDevice,
        TransferDirection::DeviceToHost})
    {
        for (const std::uint64_t byteCount : {0ULL, 1ULL, 257ULL})
        {
            const auto observation = RunCrossBackendCorrectness(direction, byteCount);
            ExpectSuccessfulPair(observation, direction, byteCount);
        }
    }
}

TEST(Ex2EIntegrationReal, ValidationPairReportsNoLayerErrors)
{
    const auto observation = RunCrossBackendCorrectness(
        TransferDirection::HostToDevice, 257U);
    ExpectSuccessfulPair(observation, TransferDirection::HostToDevice, 257U);
    EXPECT_EQ(observation.vulkanDiagnostics.validationErrorCount, 0U);
    const char* validationMode = std::getenv("COMPUTELAB_EX2_E_VALIDATION");
    if (validationMode != nullptr && std::string_view{validationMode} == "standard")
    {
        EXPECT_TRUE(observation.vulkanDiagnostics.validationEnabled);
        EXPECT_FALSE(observation.vulkanDiagnostics.synchronizationValidationEnabled);
    }
    if (validationMode != nullptr && std::string_view{validationMode} == "sync")
    {
        EXPECT_TRUE(observation.vulkanDiagnostics.validationEnabled);
        EXPECT_TRUE(observation.vulkanDiagnostics.synchronizationValidationEnabled);
    }
}

class Ex2EIntegrationNativeTiming
    : public testing::TestWithParam<TransferDirection>
{};

TEST_P(Ex2EIntegrationNativeTiming, Positive257IsASeparateBoundedMechanicsSmoke)
{
    const auto direction = GetParam();
    const auto observation = RunNativeInstrumentationSmoke(direction, 257U);
    EXPECT_TRUE(observation.Passed());
    EXPECT_TRUE(observation.physicalIdentityVerified);
    ExpectSuccessfulBackend(observation.cuda.transfer, direction, 257U);
    ExpectSuccessfulBackend(observation.vulkan.transfer, direction, 257U);
    EXPECT_EQ(observation.cuda.nativeTimingStatus, NativeTimingSmokeStatus::Valid);
    EXPECT_EQ(observation.vulkan.nativeTimingStatus, NativeTimingSmokeStatus::Valid);
    EXPECT_TRUE(observation.cuda.nativeIntervalPresent);
    EXPECT_TRUE(observation.vulkan.nativeIntervalPresent);
    EXPECT_TRUE(observation.cuda.nativeTimingMetadataValid);
    EXPECT_TRUE(observation.vulkan.nativeTimingMetadataValid);
    EXPECT_EQ(observation.cuda.nativeTimingMethod, "cudaEventElapsedTime");
    EXPECT_EQ(
        observation.vulkan.nativeTimingMethod,
        "vkCmdWriteTimestamp2/vkGetQueryPoolResults");
    EXPECT_TRUE(observation.vulkanDiagnostics.timestampQueryPoolCreated);
    EXPECT_EQ(observation.vulkanDiagnostics.validationErrorCount, 0U);
}

TEST_P(Ex2EIntegrationNativeTiming, ZeroIsNotApplicableWithoutDuration)
{
    const auto direction = GetParam();
    const auto observation = RunNativeInstrumentationSmoke(direction, 0U);
    EXPECT_TRUE(observation.Passed());
    EXPECT_EQ(
        observation.cuda.nativeTimingStatus,
        NativeTimingSmokeStatus::NotApplicable);
    EXPECT_EQ(
        observation.vulkan.nativeTimingStatus,
        NativeTimingSmokeStatus::NotApplicable);
    EXPECT_FALSE(observation.cuda.nativeIntervalPresent);
    EXPECT_FALSE(observation.vulkan.nativeIntervalPresent);
}

INSTANTIATE_TEST_SUITE_P(
    E1AndE2,
    Ex2EIntegrationNativeTiming,
    testing::Values(
        TransferDirection::HostToDevice,
        TransferDirection::DeviceToHost),
    [](const testing::TestParamInfo<Ex2EIntegrationNativeTiming::ParamType>& info) {
        return info.param == TransferDirection::HostToDevice ? "E1" : "E2";
    });

} // namespace

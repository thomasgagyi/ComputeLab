#include "app/Ex2EIntegration.hpp"

#include "ex2/Ex2CpuOracles.hpp"
#include "ex2/Ex2Input.hpp"
#include "timing/HostTiming.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace computelab::ex2::e
{
namespace
{

static_assert(cuda::Ex2CudaEOperation::InstrumentMode == "H");
static_assert(!cuda::Ex2CudaEOperation::EnqueuesDeviceTimestamps);
static_assert(vulkan::Ex2VulkanEOperation::InstrumentMode == "H");
static_assert(!vulkan::Ex2VulkanEOperation::EnqueuesDeviceTimestamps);
static_assert(cuda::Ex2CudaEDeviceTimedOperation::InstrumentMode == "N");
static_assert(cuda::Ex2CudaEDeviceTimedOperation::EnqueuesDeviceTimestamps);
static_assert(vulkan::Ex2VulkanEDeviceTimedOperation::InstrumentMode == "N");
static_assert(vulkan::Ex2VulkanEDeviceTimedOperation::EnqueuesDeviceTimestamps);

constexpr std::array<HostCaptureStep, 7U> ExpectedCaptureOrder{
    HostCaptureStep::Prepared,
    HostCaptureStep::CaptureT0,
    HostCaptureStep::Submit,
    HostCaptureStep::CaptureT1,
    HostCaptureStep::Wait,
    HostCaptureStep::CaptureT2,
    HostCaptureStep::PostCompletion};

[[nodiscard]] TransferConfiguration MakeTransferConfiguration(
    TransferDirection direction,
    std::uint64_t byteCount)
{
    return {
        direction == TransferDirection::HostToDevice
            ? TransferVariant::E1
            : TransferVariant::E2,
        byteCount,
        direction};
}

[[nodiscard]] bool HasValidSuccessfulHostTiming(
    const HostTimingIntervals& timing) noexcept
{
    if (timing.status != HostTimingStatus::Ok
        || !timing.hostSubmissionNanoseconds
        || !timing.hostWaitNanoseconds
        || !timing.hostCompletionNanoseconds)
    {
        return false;
    }
    const auto submission = *timing.hostSubmissionNanoseconds;
    const auto wait = *timing.hostWaitNanoseconds;
    return submission <= std::numeric_limits<std::uint64_t>::max() - wait
        && submission + wait == *timing.hostCompletionNanoseconds;
}

[[nodiscard]] bool IsSourceDeclared(
    std::span<const std::uint8_t> source) noexcept
{
    for (std::size_t index = 0; index < source.size(); ++index)
    {
        if (source[index] != Byte(CoreInputSeed, index))
            return false;
    }
    return true;
}

[[nodiscard]] bool ValidateVulkanResources(
    const vulkan::Ex2VulkanEDiagnostics& diagnostics,
    std::uint64_t byteCount,
    bool hMode) noexcept
{
    const bool hostVisible =
        (diagnostics.uploadMemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U
        && (diagnostics.readbackMemoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0U;
    const bool deviceLocal =
        (diagnostics.deviceMemoryFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0U;
    const bool allocationSizes = byteCount == 0U
        || (diagnostics.uploadAllocationBytes >= byteCount
            && diagnostics.deviceAllocationBytes >= byteCount
            && diagnostics.readbackAllocationBytes >= byteCount);
    const bool preparedCopyCount = diagnostics.preparedNativeCopyCount
        == (byteCount == 0U ? 0U : 1U);
    const bool memoryProperties = byteCount == 0U
        || (hostVisible && deviceLocal && allocationSizes);
    return memoryProperties
        && (diagnostics.queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0U
        && diagnostics.synchronization2Enabled
        && diagnostics.preparedCommandRecordCount == 1U
        && preparedCopyCount
        && (hMode
            ? !diagnostics.timestampQueryPoolCreated
            : byteCount == 0U || diagnostics.timestampQueryPoolCreated);
}

[[nodiscard]] const TransferConfiguration* AcceptedTransferConfiguration(
    const WorkloadConfiguration& configuration) noexcept
{
    const auto* transfer = std::get_if<TransferConfiguration>(
        &configuration.parameters);
    if (transfer == nullptr
        || configuration.common.seed != CoreInputSeed
        || configuration.common.generatorRevision != InputGeneratorRevision
        || configuration.common.executionMode != LogicalExecutionMode::Prepared
        || configuration.common.operationBoundary
            != OperationBoundary::PreparedSingleCopyCompletion
        || transfer->byteCount > std::numeric_limits<std::size_t>::max())
    {
        return nullptr;
    }
    if (transfer->direction == TransferDirection::HostToDevice)
        return transfer->variant == TransferVariant::E1 ? transfer : nullptr;
    if (transfer->direction == TransferDirection::DeviceToHost)
        return transfer->variant == TransferVariant::E2 ? transfer : nullptr;
    return nullptr;
}

[[nodiscard]] bool HasAcceptedTransferObservation(
    const BackendObservation& observation,
    const TransferConfiguration& transfer,
    bool hMode) noexcept
{
    const std::uint32_t expectedCopyCount =
        transfer.byteCount == 0U ? 0U : 1U;
    const bool expectedCopyExecuted = transfer.byteCount != 0U;
    return observation.status == IntegrationStatus::Ok
        && !observation.failurePhase
        && !observation.errorCode
        && !observation.nativeErrorCode
        && observation.nativeErrorName.empty()
        && observation.nativeOperation.empty()
        && observation.safeForFurtherGpuCalls
        && observation.nativeTransferCompleted
        && observation.destinationObserved
        && observation.cpuComparisonPerformed
        && observation.validationPassed == std::optional<bool>{true}
        && observation.direction == transfer.direction
        && observation.expectedByteCount == transfer.byteCount
        && observation.expectedNativeCopyCount == expectedCopyCount
        && observation.completedNativeCopyCount
            == std::optional<std::uint32_t>{expectedCopyCount}
        && observation.copyExecuted
            == std::optional<bool>{expectedCopyExecuted}
        && observation.hModeUninstrumented == hMode
        && observation.hostCaptureComplete
        && HasValidSuccessfulHostTiming(observation.hostTiming)
        && observation.destination.size() == transfer.byteCount;
}

[[nodiscard]] BackendObservation FailureObservation(
    const FailureClassification& classification,
    TransferDirection direction,
    std::uint64_t byteCount,
    bool nativeTransferCompleted,
    bool destinationObserved,
    HostTimingIntervals hostTiming,
    std::optional<std::int64_t> nativeErrorCode = std::nullopt,
    std::string nativeErrorName = {},
    std::string nativeOperation = {})
{
    BackendObservation result;
    result.nativeTransferCompleted = nativeTransferCompleted;
    result.destinationObserved = destinationObserved;
    result.status = classification.status;
    result.failurePhase = classification.phase;
    result.errorCode = classification.errorCode;
    result.nativeErrorCode = nativeErrorCode;
    result.nativeErrorName = std::move(nativeErrorName);
    result.nativeOperation = std::move(nativeOperation);
    result.safeForFurtherGpuCalls = classification.safeForFurtherGpuCalls;
    result.direction = direction;
    result.expectedByteCount = byteCount;
    result.expectedNativeCopyCount = byteCount == 0U ? 0U : 1U;
    result.hostTiming = std::move(hostTiming);
    result.hostCaptureComplete = result.hostTiming.status == HostTimingStatus::Ok;
    result.validationPassed = false;
    return result;
}

[[nodiscard]] FailureClassification UnclassifiedFailure(bool safe) noexcept
{
    return {
        IntegrationStatus::Incomplete,
        IntegrationFailurePhase::NativeFailure,
        IntegrationErrorCode::UnclassifiedNativeFailure,
        safe};
}

template<typename Operation>
[[nodiscard]] BackendObservation ExecuteCudaTransfer(
    Operation& operation,
    std::span<const std::uint8_t> source,
    std::span<const std::uint8_t> originalSource,
    std::span<const std::uint8_t> expectedDestination,
    bool hMode,
    NativeTimingSmokeBackendObservation* smoke)
{
    const auto direction = operation.Direction();
    const auto byteCount = operation.ByteCount();
    try
    {
        operation.PrepareTransfer(source);
    }
    catch (const cuda::Ex2CudaENativeError& error)
    {
        return FailureObservation(
            ClassifyCudaFailure(error.Phase(), error.NativeErrorCode()),
            direction,
            byteCount,
            false,
            false,
            CalculateHostTimingIntervals(
                HostTimingStatus::Incomplete, std::nullopt, std::nullopt, std::nullopt),
            error.NativeErrorCode(),
            error.NativeErrorName());
    }

    std::optional<timing::HostTimePoint> t0;
    std::optional<timing::HostTimePoint> t1;
    std::optional<timing::HostTimePoint> t2;
    try
    {
        t0 = timing::CaptureHostTime();
        operation.SubmitTransfer();
        t1 = timing::CaptureHostTime();
        operation.WaitForCompletion();
        t2 = timing::CaptureHostTime();
    }
    catch (const cuda::Ex2CudaENativeError& error)
    {
        const HostTimingStatus timingStatus = t1
            ? HostTimingStatus::WaitFailed
            : HostTimingStatus::SubmitFailed;
        return FailureObservation(
            ClassifyCudaFailure(error.Phase(), error.NativeErrorCode()),
            direction,
            byteCount,
            false,
            false,
            CalculateHostTimingIntervals(timingStatus, t0, t1, std::nullopt),
            error.NativeErrorCode(),
            error.NativeErrorName());
    }

    BackendObservation result;
    result.nativeTransferCompleted = true;
    result.direction = direction;
    result.expectedByteCount = byteCount;
    result.expectedNativeCopyCount = operation.ExpectedNativeCopyCount();
    result.hModeUninstrumented = hMode
        && Operation::InstrumentMode == "H"
        && !Operation::EnqueuesDeviceTimestamps;
    result.hostTiming = CalculateHostTimingIntervals(
        HostTimingStatus::Ok, t0, t1, t2);
    result.hostCaptureComplete = HasValidSuccessfulHostTiming(result.hostTiming);

    try
    {
        result.completedNativeCopyCount = operation.LastCompletionNativeCopyCount();
        result.copyExecuted = operation.LastCompletionExecutedCopy();
        if constexpr (requires(Operation& value) {
            value.RetrieveNativeTiming();
            value.NativeTimingStatus();
            value.NativeTimingMetadata();
            value.NativeDeviceIntervalNanoseconds();
        })
        {
            if (smoke != nullptr)
            {
                operation.RetrieveNativeTiming();
                const auto status = operation.NativeTimingStatus();
                const auto interval = operation.NativeDeviceIntervalNanoseconds();
                const auto metadata = operation.NativeTimingMetadata();
                smoke->nativeTimingRetrieved = true;
                smoke->nativeIntervalPresent = interval.has_value();
                smoke->nativeTimingMethod = std::string{metadata.method};
                smoke->nativeTimingMetadataValid =
                    metadata.method == "cudaEventElapsedTime"
                    && metadata.eventCreateFlags == "cudaEventDefault (timing enabled)"
                    && metadata.elapsedValueRepresentation == "float milliseconds"
                    && metadata.approximateResolutionNanoseconds == 500U;
                smoke->nativeTimingStatus = byteCount == 0U
                    && status == cuda::Ex2CudaENativeTimingStatus::NotApplicable
                    && !interval
                    ? NativeTimingSmokeStatus::NotApplicable
                    : status == cuda::Ex2CudaENativeTimingStatus::Valid && interval
                        ? NativeTimingSmokeStatus::Valid
                        : NativeTimingSmokeStatus::Failed;
            }
        }
        result.destination = operation.RetrieveDestinationForValidation();
        result.destinationObserved = true;
    }
    catch (const cuda::Ex2CudaENativeError& error)
    {
        auto failure = FailureObservation(
            ClassifyCudaFailure(error.Phase(), error.NativeErrorCode()),
            direction,
            byteCount,
            true,
            result.destinationObserved,
            result.hostTiming,
            error.NativeErrorCode(),
            error.NativeErrorName());
        failure.completedNativeCopyCount = result.completedNativeCopyCount;
        failure.copyExecuted = result.copyExecuted;
        return failure;
    }

    const bool expectedExecuted = byteCount != 0U;
    if (hMode)
    {
        const auto validation = ValidateCompletedResult(
            originalSource,
            source,
            expectedDestination,
            result.destination,
            result.expectedNativeCopyCount,
            *result.completedNativeCopyCount,
            expectedExecuted,
            *result.copyExecuted,
            result.hModeUninstrumented,
            result.hostTiming);
        result.cpuComparisonPerformed = true;
        result.validationPassed = validation.Passed();
    }
    else
    {
        result.cpuComparisonPerformed = true;
        result.validationPassed = result.destination.size() == expectedDestination.size()
            && std::ranges::equal(result.destination, expectedDestination)
            && std::ranges::equal(source, originalSource)
            && *result.completedNativeCopyCount == result.expectedNativeCopyCount
            && *result.copyExecuted == expectedExecuted
            && result.hostCaptureComplete
            && smoke != nullptr
            && smoke->nativeTimingMetadataValid
            && smoke->nativeTimingStatus != NativeTimingSmokeStatus::Failed;
    }
    result.status = *result.validationPassed
        ? IntegrationStatus::Ok
        : IntegrationStatus::ValidationFailed;
    result.safeForFurtherGpuCalls = true;
    if (!*result.validationPassed)
    {
        result.failurePhase = IntegrationFailurePhase::ValidationMismatch;
        result.errorCode = IntegrationErrorCode::DestinationMismatch;
    }
    return result;
}

template<typename Operation>
[[nodiscard]] BackendObservation ExecuteVulkanTransfer(
    Operation& operation,
    std::span<const std::uint8_t> source,
    std::span<const std::uint8_t> originalSource,
    std::span<const std::uint8_t> expectedDestination,
    bool hMode,
    NativeTimingSmokeBackendObservation* smoke)
{
    const auto direction = operation.Direction();
    const auto byteCount = operation.ByteCount();
    try
    {
        operation.PrepareTransfer(source);
    }
    catch (const vulkan::Ex2VulkanENativeError& error)
    {
        return FailureObservation(
            ClassifyVulkanFailure(error.Phase(), error.NativeResult()),
            direction,
            byteCount,
            false,
            false,
            CalculateHostTimingIntervals(
                HostTimingStatus::Incomplete, std::nullopt, std::nullopt, std::nullopt),
            static_cast<std::int64_t>(error.NativeResult()),
            {},
            error.Operation());
    }

    std::optional<timing::HostTimePoint> t0;
    std::optional<timing::HostTimePoint> t1;
    std::optional<timing::HostTimePoint> t2;
    try
    {
        t0 = timing::CaptureHostTime();
        operation.SubmitTransfer();
        t1 = timing::CaptureHostTime();
        operation.WaitForCompletion();
        t2 = timing::CaptureHostTime();
    }
    catch (const vulkan::Ex2VulkanENativeError& error)
    {
        const HostTimingStatus timingStatus = error.NativeResult() == VK_TIMEOUT
            ? HostTimingStatus::Timeout
            : t1 ? HostTimingStatus::WaitFailed : HostTimingStatus::SubmitFailed;
        return FailureObservation(
            ClassifyVulkanFailure(error.Phase(), error.NativeResult()),
            direction,
            byteCount,
            false,
            false,
            CalculateHostTimingIntervals(timingStatus, t0, t1, std::nullopt),
            static_cast<std::int64_t>(error.NativeResult()),
            {},
            error.Operation());
    }

    BackendObservation result;
    result.nativeTransferCompleted = true;
    result.direction = direction;
    result.expectedByteCount = byteCount;
    result.expectedNativeCopyCount = operation.ExpectedNativeCopyCount();
    result.hModeUninstrumented = hMode
        && Operation::InstrumentMode == "H"
        && !Operation::EnqueuesDeviceTimestamps
        && !operation.Diagnostics().timestampQueryPoolCreated;
    result.hostTiming = CalculateHostTimingIntervals(
        HostTimingStatus::Ok, t0, t1, t2);
    result.hostCaptureComplete = HasValidSuccessfulHostTiming(result.hostTiming);

    try
    {
        result.completedNativeCopyCount = operation.LastCompletionNativeCopyCount();
        result.copyExecuted = operation.LastCompletionExecutedCopy();
        if constexpr (requires(Operation& value) {
            value.RetrieveNativeTiming();
            value.NativeTimingStatus();
            value.NativeTimingMetadata();
            value.NativeDeviceIntervalNanoseconds();
        })
        {
            if (smoke != nullptr)
            {
                operation.RetrieveNativeTiming();
                const auto status = operation.NativeTimingStatus();
                const auto interval = operation.NativeDeviceIntervalNanoseconds();
                const auto metadata = operation.NativeTimingMetadata();
                smoke->nativeTimingRetrieved = true;
                smoke->nativeIntervalPresent = interval.has_value();
                smoke->nativeTimingMethod = std::string{metadata.method};
                smoke->nativeTimingMetadataValid =
                    metadata.method == "vkCmdWriteTimestamp2/vkGetQueryPoolResults"
                    && std::isfinite(metadata.timestampPeriodNanoseconds)
                    && metadata.timestampPeriodNanoseconds > 0.0L
                    && metadata.timestampValidBits >= 36U
                    && metadata.timestampValidBits <= 64U
                    && metadata.startStage == VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT
                    && metadata.stopStage == VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT
                    && metadata.durationEnvelopeNanoseconds
                        == vulkan::Ex2VulkanEDurationEnvelopeNanoseconds;
                smoke->nativeTimingStatus = byteCount == 0U
                    && status == vulkan::Ex2VulkanENativeTimingStatus::NotApplicable
                    && !interval
                    ? NativeTimingSmokeStatus::NotApplicable
                    : status == vulkan::Ex2VulkanENativeTimingStatus::Valid && interval
                        ? NativeTimingSmokeStatus::Valid
                        : NativeTimingSmokeStatus::Failed;
            }
        }
        result.destination = operation.RetrieveDestinationForValidation();
        result.destinationObserved = true;
    }
    catch (const vulkan::Ex2VulkanENativeError& error)
    {
        auto failure = FailureObservation(
            ClassifyVulkanFailure(error.Phase(), error.NativeResult()),
            direction,
            byteCount,
            true,
            result.destinationObserved,
            result.hostTiming,
            static_cast<std::int64_t>(error.NativeResult()),
            {},
            error.Operation());
        failure.completedNativeCopyCount = result.completedNativeCopyCount;
        failure.copyExecuted = result.copyExecuted;
        return failure;
    }

    const bool resourceInvariants = ValidateVulkanResources(
        operation.Diagnostics(), byteCount, hMode);
    const bool expectedExecuted = byteCount != 0U;
    if (hMode)
    {
        const auto validation = ValidateCompletedResult(
            originalSource,
            source,
            expectedDestination,
            result.destination,
            result.expectedNativeCopyCount,
            *result.completedNativeCopyCount,
            expectedExecuted,
            *result.copyExecuted,
            result.hModeUninstrumented,
            result.hostTiming,
            resourceInvariants);
        result.cpuComparisonPerformed = true;
        result.validationPassed = validation.Passed();
    }
    else
    {
        result.cpuComparisonPerformed = true;
        result.validationPassed = result.destination.size() == expectedDestination.size()
            && std::ranges::equal(result.destination, expectedDestination)
            && std::ranges::equal(source, originalSource)
            && *result.completedNativeCopyCount == result.expectedNativeCopyCount
            && *result.copyExecuted == expectedExecuted
            && result.hostCaptureComplete
            && resourceInvariants
            && smoke != nullptr
            && smoke->nativeTimingMetadataValid
            && smoke->nativeTimingStatus != NativeTimingSmokeStatus::Failed;
    }
    result.status = *result.validationPassed
        ? IntegrationStatus::Ok
        : IntegrationStatus::ValidationFailed;
    result.safeForFurtherGpuCalls = true;
    if (!*result.validationPassed)
    {
        result.failurePhase = IntegrationFailurePhase::ValidationMismatch;
        result.errorCode = IntegrationErrorCode::DestinationMismatch;
    }
    return result;
}

void PreserveUnsafeOperations(
    std::unique_ptr<cuda::Ex2CudaEOperation>& cudaOperation,
    std::unique_ptr<vulkan::Ex2VulkanEOperation>& vulkanOperation) noexcept
{
    static_cast<void>(cudaOperation.release());
    static_cast<void>(vulkanOperation.release());
}

void PreserveUnsafeTimedOperations(
    std::unique_ptr<cuda::Ex2CudaEDeviceTimedOperation>& cudaOperation,
    std::unique_ptr<vulkan::Ex2VulkanEDeviceTimedOperation>& vulkanOperation) noexcept
{
    static_cast<void>(cudaOperation.release());
    static_cast<void>(vulkanOperation.release());
}

[[nodiscard]] CrossBackendObservation RunCrossBackendWithOwnedData(
    TransferDirection direction,
    std::vector<std::uint8_t> source,
    std::vector<std::uint8_t> expectedDestination,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    correctness::control::AttemptObserver observer)
{
    if (source.size() != expectedDestination.size())
        throw std::invalid_argument("EX-2 E source/expectation length mismatch");
    const auto configuration = MakeConfiguration(
        MakeTransferConfiguration(direction, source.size()));
    ValidateDeclaredSource(configuration, source);

    auto cudaOperation = std::make_unique<cuda::Ex2CudaEOperation>(
        cudaDeviceOrdinal,
        std::get<TransferConfiguration>(configuration.parameters));
    auto vulkanOperation = std::make_unique<vulkan::Ex2VulkanEOperation>(
        std::get<TransferConfiguration>(configuration.parameters),
        vulkanPhysicalDeviceIndex);
    const DeviceUuid cudaUuid = cudaOperation->SelectedDeviceUuid();
    const DeviceUuid vulkanUuid = vulkanOperation->SelectedDeviceUuid();
    const auto uuidText = VerifySamePhysicalDevice(cudaUuid, vulkanUuid);
    if (vulkanOperation->Diagnostics().deviceUuid != vulkanUuid)
        throw std::runtime_error("EX-2 E Vulkan diagnostics UUID mismatch");

    CrossBackendObservation result;
    result.configuration = configuration;
    result.verifiedDeviceUuid = cudaUuid;
    result.verifiedDeviceUuidText = uuidText;
    result.physicalIdentityVerified = true;
    result.source = std::move(source);
    result.expectedDestination = std::move(expectedDestination);
    result.sourceSha256 = ByteInputSha256(result.source);
    result.expectedDestinationSha256 = ByteInputSha256(
        result.expectedDestination);
    const std::vector<std::uint8_t> originalSource = result.source;

    observer.Report(correctness::control::AttemptBackend::Cuda,
        correctness::control::AttemptEvent::Started);
    result.cuda = ExecuteCudaTransfer(
        *cudaOperation,
        result.source,
        originalSource,
        result.expectedDestination,
        true,
        nullptr);
    observer.Report(correctness::control::AttemptBackend::Cuda,
        correctness::control::AttemptEvent::Returned);
    if (result.cuda.status != IntegrationStatus::Ok)
    {
        result.vulkan = MakeInterruptedObservation(
            direction, result.source.size(), result.cuda.safeForFurtherGpuCalls);
        if (!result.cuda.safeForFurtherGpuCalls)
            PreserveUnsafeOperations(cudaOperation, vulkanOperation);
        return result;
    }

    observer.Report(correctness::control::AttemptBackend::Vulkan,
        correctness::control::AttemptEvent::Started);
    result.vulkan = ExecuteVulkanTransfer(
        *vulkanOperation,
        result.source,
        originalSource,
        result.expectedDestination,
        true,
        nullptr);
    observer.Report(correctness::control::AttemptBackend::Vulkan,
        correctness::control::AttemptEvent::Returned);
    result.vulkanDiagnostics = vulkanOperation->Diagnostics();
    ReconcileCrossBackendDestinations(result);
    if (!result.vulkan.safeForFurtherGpuCalls)
        PreserveUnsafeOperations(cudaOperation, vulkanOperation);
    return result;
}

} // namespace

bool CompletedValidation::Passed() const noexcept
{
    return destinationLengthMatches
        && destinationMatchesExpected
        && sourceMatchesDeclaration
        && sourcePreserved
        && copyCountMatches
        && executedCopyMatches
        && hModeUninstrumented
        && hostTimingValid
        && vulkanResourceInvariants;
}

bool CrossBackendObservation::Passed() const noexcept
{
    const auto* transfer = AcceptedTransferConfiguration(configuration);
    if (transfer == nullptr)
        return false;
    const bool vulkanResourcesAccepted = ValidateVulkanResources(
        vulkanDiagnostics, transfer->byteCount, true);
    return physicalIdentityVerified
        && !IsZeroDeviceUuid(verifiedDeviceUuid)
        && vulkanDiagnostics.deviceUuid == verifiedDeviceUuid
        && !sourceSha256.empty()
        && !expectedDestinationSha256.empty()
        && source.size() == transfer->byteCount
        && expectedDestination.size() == transfer->byteCount
        && IsSourceDeclared(source)
        && std::ranges::equal(source, expectedDestination)
        && HasAcceptedTransferObservation(cuda, *transfer, true)
        && HasAcceptedTransferObservation(vulkan, *transfer, true)
        && std::ranges::equal(cuda.destination, expectedDestination)
        && std::ranges::equal(vulkan.destination, expectedDestination)
        && std::ranges::equal(cuda.destination, vulkan.destination)
        && pairComparisonPerformed
        && pairDestinationsEqual
        && vulkanResourcesAccepted
        && vulkanDiagnostics.validationErrorCount == 0U;
}

bool NativeInstrumentationSmokeObservation::Passed() const noexcept
{
    const auto* transfer = AcceptedTransferConfiguration(configuration);
    if (transfer == nullptr)
        return false;
    const bool zero = transfer->byteCount == 0U;
    const auto timingExpected = zero
        ? NativeTimingSmokeStatus::NotApplicable
        : NativeTimingSmokeStatus::Valid;
    return physicalIdentityVerified
        && !IsZeroDeviceUuid(verifiedDeviceUuid)
        && vulkanDiagnostics.deviceUuid == verifiedDeviceUuid
        && HasAcceptedTransferObservation(cuda.transfer, *transfer, false)
        && HasAcceptedTransferObservation(vulkan.transfer, *transfer, false)
        && cuda.nativeTimingStatus == timingExpected
        && vulkan.nativeTimingStatus == timingExpected
        && cuda.nativeTimingRetrieved
        && vulkan.nativeTimingRetrieved
        && cuda.nativeTimingMetadataValid
        && vulkan.nativeTimingMetadataValid
        && cuda.nativeTimingMethod == "cudaEventElapsedTime"
        && vulkan.nativeTimingMethod
            == "vkCmdWriteTimestamp2/vkGetQueryPoolResults"
        && cuda.nativeIntervalPresent == !zero
        && vulkan.nativeIntervalPresent == !zero
        && std::ranges::equal(
            cuda.transfer.destination, vulkan.transfer.destination)
        && pairComparisonPerformed
        && pairDestinationsEqual
        && ValidateVulkanResources(
            vulkanDiagnostics, transfer->byteCount, false)
        && vulkanDiagnostics.validationErrorCount == 0U;
}

bool IsZeroDeviceUuid(const DeviceUuid& uuid) noexcept
{
    return std::all_of(uuid.begin(), uuid.end(), [](std::uint8_t byte) {
        return byte == 0U;
    });
}

std::string FormatDeviceUuid(const DeviceUuid& uuid)
{
    constexpr char Hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(32U);
    for (const auto byte : uuid)
    {
        result.push_back(Hex[byte >> 4U]);
        result.push_back(Hex[byte & 0x0FU]);
    }
    return result;
}

std::string VerifySamePhysicalDevice(
    const DeviceUuid& cudaUuid,
    const DeviceUuid& vulkanUuid)
{
    if (IsZeroDeviceUuid(cudaUuid) || IsZeroDeviceUuid(vulkanUuid))
        throw std::runtime_error("EX-2 E integration requires nonzero device UUIDs");
    if (cudaUuid != vulkanUuid)
        throw std::runtime_error("EX-2 E CUDA/Vulkan physical-device UUID mismatch");
    return FormatDeviceUuid(cudaUuid);
}

void ValidateDeclaredSource(
    const WorkloadConfiguration& configuration,
    std::span<const std::uint8_t> source)
{
    const auto* transfer = std::get_if<TransferConfiguration>(
        &configuration.parameters);
    if (transfer == nullptr
        || !ValidateSemanticConfiguration(configuration).IsValid()
        || !IsCorrectnessTestEligible(configuration)
        || configuration.common.seed != CoreInputSeed
        || configuration.common.generatorRevision != InputGeneratorRevision
        || configuration.common.executionMode != LogicalExecutionMode::Prepared
        || configuration.common.operationBoundary
            != OperationBoundary::PreparedSingleCopyCompletion
        || transfer->byteCount != source.size()
        || !IsSourceDeclared(source))
    {
        throw std::invalid_argument("invalid EX-2 E declared source/configuration");
    }
}

bool IsValidHostCaptureOrder(
    std::span<const HostCaptureStep> steps) noexcept
{
    return std::ranges::equal(steps, ExpectedCaptureOrder);
}

CompletedValidation ValidateCompletedResult(
    std::span<const std::uint8_t> declaredSource,
    std::span<const std::uint8_t> sourceAfter,
    std::span<const std::uint8_t> expectedDestination,
    std::span<const std::uint8_t> observedDestination,
    std::uint32_t expectedNativeCopyCount,
    std::uint32_t completedNativeCopyCount,
    bool expectedCopyExecuted,
    bool copyExecuted,
    bool hModeUninstrumented,
    const HostTimingIntervals& hostTiming,
    bool vulkanResourceInvariants) noexcept
{
    return {
        observedDestination.size() == expectedDestination.size(),
        std::ranges::equal(observedDestination, expectedDestination),
        IsSourceDeclared(declaredSource),
        std::ranges::equal(sourceAfter, declaredSource),
        completedNativeCopyCount == expectedNativeCopyCount,
        copyExecuted == expectedCopyExecuted,
        hModeUninstrumented,
        HasValidSuccessfulHostTiming(hostTiming),
        vulkanResourceInvariants};
}

void ReconcileCrossBackendDestinations(CrossBackendObservation& observation)
{
    if (!observation.cuda.destinationObserved
        || !observation.vulkan.destinationObserved
        || !observation.cuda.cpuComparisonPerformed
        || !observation.vulkan.cpuComparisonPerformed)
    {
        observation.pairComparisonPerformed = false;
        observation.pairDestinationsEqual = false;
        return;
    }
    observation.pairComparisonPerformed = true;
    observation.pairDestinationsEqual =
        observation.cuda.destination.size() == observation.source.size()
        && observation.vulkan.destination.size() == observation.source.size()
        && observation.cuda.destination == observation.vulkan.destination;
}

BackendObservation MakeInterruptedObservation(
    TransferDirection direction,
    std::uint64_t byteCount,
    bool sharedDeviceKnownSafe) noexcept
{
    BackendObservation result;
    result.status = IntegrationStatus::Incomplete;
    result.failurePhase = IntegrationFailurePhase::InterruptedSecondBackend;
    result.errorCode = IntegrationErrorCode::Interrupted;
    result.safeForFurtherGpuCalls = sharedDeviceKnownSafe;
    result.direction = direction;
    result.expectedByteCount = byteCount;
    result.expectedNativeCopyCount = byteCount == 0U ? 0U : 1U;
    return result;
}

FailedSessionResourceDisposition ClassifyFailedSessionResourceDisposition(
    const BackendObservation& failedObservation) noexcept
{
    return failedObservation.safeForFurtherGpuCalls
        ? FailedSessionResourceDisposition::DestroyNormally
        : FailedSessionResourceDisposition::PreserveForProcessTeardown;
}

FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaENativePhase phase,
    int nativeErrorCode) noexcept
{
    using Phase = cuda::Ex2CudaENativePhase;
    FailureClassification result;
    switch (phase)
    {
    case Phase::DeviceSelection:
    case Phase::RuntimeInitialization:
    case Phase::DeviceProperties:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::BackendInitialization, IntegrationErrorCode::BackendInitializationFailed, true};
        break;
    case Phase::ResourcePreflight:
    case Phase::StreamCreation:
    case Phase::PinnedSourceAllocation:
    case Phase::PinnedDestinationAllocation:
    case Phase::DeviceAllocation:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::ResourceAllocation, IntegrationErrorCode::ResourceAllocationFailed, true};
        break;
    case Phase::HostSourcePreparation:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::HostSourcePreparation, IntegrationErrorCode::HostSourcePreparationFailed, true};
        break;
    case Phase::DeviceSourcePreparation:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::DeviceSourcePreparation, IntegrationErrorCode::DeviceSourcePreparationFailed, false};
        break;
    case Phase::PreparationCompletion:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::PreparationCompletion, IntegrationErrorCode::PreparationCompletionFailed, false};
        break;
    case Phase::StartMarker:
        result = {IntegrationStatus::SubmitFailed, IntegrationFailurePhase::StartMarker, IntegrationErrorCode::StartMarkerFailed, true};
        break;
    case Phase::TransferSubmission:
        result = {IntegrationStatus::SubmitFailed, IntegrationFailurePhase::TransferSubmission, IntegrationErrorCode::SubmissionFailed, false};
        break;
    case Phase::StopMarker:
        result = {IntegrationStatus::SubmitFailed, IntegrationFailurePhase::StopMarker, IntegrationErrorCode::StopMarkerFailed, false};
        break;
    case Phase::CompletionWait:
        result = {IntegrationStatus::WaitFailed, IntegrationFailurePhase::CompletionWait, IntegrationErrorCode::CompletionFailed, false};
        break;
    case Phase::ValidationReadback:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::ValidationReadback, IntegrationErrorCode::ValidationReadbackFailed, false};
        break;
    case Phase::ValidationReadbackCompletion:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::ValidationReadbackCompletion, IntegrationErrorCode::ValidationReadbackCompletionFailed, false};
        break;
    case Phase::NativeTimingRetrieval:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::NativeTimingRetrieval, IntegrationErrorCode::NativeTimingRetrievalFailed, true};
        break;
    case Phase::NativeTimingConversion:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::NativeTimingConversion, IntegrationErrorCode::NativeTimingConversionFailed, true};
        break;
    default:
        result = UnclassifiedFailure(false);
        break;
    }
    result.nativeErrorCode = nativeErrorCode;
    return result;
}

FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanENativePhase phase,
    VkResult nativeResult) noexcept
{
    using Phase = vulkan::Ex2VulkanENativePhase;
    FailureClassification result;
    switch (phase)
    {
    case Phase::InstanceCreation:
    case Phase::DeviceEnumeration:
    case Phase::DeviceSelection:
    case Phase::DeviceProperties:
    case Phase::QueueSelection:
    case Phase::LogicalDeviceCreation:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::BackendInitialization, IntegrationErrorCode::BackendInitializationFailed, true};
        break;
    case Phase::ResourcePreflight:
    case Phase::BufferCreation:
    case Phase::MemoryAllocation:
    case Phase::MemoryBinding:
    case Phase::MemoryMapping:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::ResourceAllocation, IntegrationErrorCode::ResourceAllocationFailed, true};
        break;
    case Phase::HostSourcePreparation:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::HostSourcePreparation, IntegrationErrorCode::HostSourcePreparationFailed, true};
        break;
    case Phase::NoncoherentFlush:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::NoncoherentFlush, IntegrationErrorCode::NoncoherentFlushFailed, true};
        break;
    case Phase::DeviceSourcePreparation:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::DeviceSourcePreparation, IntegrationErrorCode::DeviceSourcePreparationFailed, false};
        break;
    case Phase::PreparationCompletion:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::PreparationCompletion, IntegrationErrorCode::PreparationCompletionFailed, true};
        break;
    case Phase::CommandCreation:
    case Phase::CommandRecording:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::CommandPreparation, IntegrationErrorCode::CommandPreparationFailed, true};
        break;
    case Phase::QueryReset:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::QueryReset, IntegrationErrorCode::QueryResetFailed, true};
        break;
    case Phase::Submission:
        result = {IntegrationStatus::SubmitFailed, IntegrationFailurePhase::TransferSubmission, IntegrationErrorCode::SubmissionFailed, false};
        break;
    case Phase::CompletionWait:
        result = {IntegrationStatus::WaitFailed, IntegrationFailurePhase::CompletionWait, IntegrationErrorCode::CompletionFailed, false};
        break;
    case Phase::ValidationReadback:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::ValidationReadback, IntegrationErrorCode::ValidationReadbackFailed, false};
        break;
    case Phase::HostReadVisibility:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::HostReadVisibility, IntegrationErrorCode::HostReadVisibilityFailed, false};
        break;
    case Phase::NoncoherentInvalidate:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::NoncoherentInvalidate, IntegrationErrorCode::NoncoherentInvalidateFailed, true};
        break;
    case Phase::NativeTimingRetrieval:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::NativeTimingRetrieval, IntegrationErrorCode::NativeTimingRetrievalFailed, true};
        break;
    case Phase::NativeTimingConversion:
        result = {IntegrationStatus::Incomplete, IntegrationFailurePhase::NativeTimingConversion, IntegrationErrorCode::NativeTimingConversionFailed, true};
        break;
    default:
        result = UnclassifiedFailure(false);
        break;
    }
    if (nativeResult == VK_TIMEOUT)
    {
        result.status = IntegrationStatus::Timeout;
        result.errorCode = IntegrationErrorCode::OperationTimeout;
        result.safeForFurtherGpuCalls = false;
    }
    else if (nativeResult == VK_ERROR_DEVICE_LOST)
    {
        result.status = IntegrationStatus::DeviceLost;
        result.errorCode = IntegrationErrorCode::DeviceLost;
        result.safeForFurtherGpuCalls = false;
    }
    result.nativeErrorCode = static_cast<std::int64_t>(nativeResult);
    return result;
}

CrossBackendObservation RunCrossBackendCorrectness(
    TransferDirection direction,
    std::uint64_t byteCount,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    correctness::control::AttemptObserver observer)
{
    TransferReference reference = ReferenceTransfer(
        CoreInputSeed, byteCount, direction);
    return RunCrossBackendWithOwnedData(
        direction,
        std::move(reference.source),
        std::move(reference.expectedDestination),
        cudaDeviceOrdinal,
        vulkanPhysicalDeviceIndex, observer);
}

CrossBackendObservation RunCrossBackendCorrectnessWithDeclaredData(
    TransferDirection direction,
    std::span<const std::uint8_t> source,
    std::span<const std::uint8_t> expectedDestination,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    correctness::control::AttemptObserver observer)
{
    return RunCrossBackendWithOwnedData(
        direction,
        std::vector<std::uint8_t>{source.begin(), source.end()},
        std::vector<std::uint8_t>{
            expectedDestination.begin(), expectedDestination.end()},
        cudaDeviceOrdinal,
        vulkanPhysicalDeviceIndex, observer);
}

NativeInstrumentationSmokeObservation RunNativeInstrumentationSmoke(
    TransferDirection direction,
    std::uint64_t byteCount,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex)
{
    const TransferReference reference = ReferenceTransfer(
        CoreInputSeed, byteCount, direction);
    const auto configuration = MakeConfiguration(
        MakeTransferConfiguration(direction, byteCount));
    ValidateDeclaredSource(configuration, reference.source);
    auto cudaOperation = std::make_unique<cuda::Ex2CudaEDeviceTimedOperation>(
        cudaDeviceOrdinal,
        std::get<TransferConfiguration>(configuration.parameters));
    auto vulkanOperation =
        std::make_unique<vulkan::Ex2VulkanEDeviceTimedOperation>(
            std::get<TransferConfiguration>(configuration.parameters),
            vulkanPhysicalDeviceIndex);
    const DeviceUuid cudaUuid = cudaOperation->SelectedDeviceUuid();
    const DeviceUuid vulkanUuid = vulkanOperation->SelectedDeviceUuid();

    NativeInstrumentationSmokeObservation result;
    result.configuration = configuration;
    result.verifiedDeviceUuidText = VerifySamePhysicalDevice(cudaUuid, vulkanUuid);
    if (vulkanOperation->Diagnostics().deviceUuid != vulkanUuid)
        throw std::runtime_error("EX-2 E Vulkan diagnostics UUID mismatch");
    result.verifiedDeviceUuid = cudaUuid;
    result.physicalIdentityVerified = true;
    const std::vector<std::uint8_t> originalSource = reference.source;
    result.cuda.transfer = ExecuteCudaTransfer(
        *cudaOperation,
        reference.source,
        originalSource,
        reference.expectedDestination,
        false,
        &result.cuda);
    if (result.cuda.transfer.status != IntegrationStatus::Ok)
    {
        result.vulkan.transfer = MakeInterruptedObservation(
            direction, byteCount, result.cuda.transfer.safeForFurtherGpuCalls);
        if (!result.cuda.transfer.safeForFurtherGpuCalls)
            PreserveUnsafeTimedOperations(cudaOperation, vulkanOperation);
        return result;
    }
    result.vulkan.transfer = ExecuteVulkanTransfer(
        *vulkanOperation,
        reference.source,
        originalSource,
        reference.expectedDestination,
        false,
        &result.vulkan);
    result.vulkanDiagnostics = vulkanOperation->Diagnostics();
    if (result.vulkan.transfer.destinationObserved
        && result.cuda.transfer.destinationObserved
        && result.vulkan.transfer.cpuComparisonPerformed
        && result.cuda.transfer.cpuComparisonPerformed)
    {
        result.pairComparisonPerformed = true;
        result.pairDestinationsEqual =
            result.cuda.transfer.destination == result.vulkan.transfer.destination;
    }
    if (!result.vulkan.transfer.safeForFurtherGpuCalls)
        PreserveUnsafeTimedOperations(cudaOperation, vulkanOperation);
    return result;
}

} // namespace computelab::ex2::e

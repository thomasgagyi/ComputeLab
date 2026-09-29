#pragma once

#include "cuda/Ex2CudaE.hpp"
#include "ex2/Ex2Configuration.hpp"
#include "ex2/Ex2HostTiming.hpp"
#include "vulkan/Ex2VulkanE.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::ex2::e
{

using DeviceUuid = std::array<std::uint8_t, 16>;

enum class IntegrationStatus
{
    Incomplete,
    Ok,
    ValidationFailed,
    SubmitFailed,
    WaitFailed,
    Timeout,
    DeviceLost,
};

enum class IntegrationFailurePhase
{
    ConfigurationValidation,
    BackendInitialization,
    ResourceAllocation,
    HostSourcePreparation,
    NoncoherentFlush,
    DeviceSourcePreparation,
    PreparationCompletion,
    StartMarker,
    TransferSubmission,
    StopMarker,
    CommandPreparation,
    QueryReset,
    CompletionWait,
    ValidationReadback,
    ValidationReadbackCompletion,
    HostReadVisibility,
    NoncoherentInvalidate,
    NativeTimingRetrieval,
    NativeTimingConversion,
    ValidationMismatch,
    InterruptedSecondBackend,
    NativeFailure,
};

enum class IntegrationErrorCode
{
    InvalidConfiguration,
    BackendInitializationFailed,
    ResourceAllocationFailed,
    HostSourcePreparationFailed,
    NoncoherentFlushFailed,
    DeviceSourcePreparationFailed,
    PreparationCompletionFailed,
    StartMarkerFailed,
    SubmissionFailed,
    StopMarkerFailed,
    CommandPreparationFailed,
    QueryResetFailed,
    CompletionFailed,
    OperationTimeout,
    DeviceLost,
    ValidationReadbackFailed,
    ValidationReadbackCompletionFailed,
    HostReadVisibilityFailed,
    NoncoherentInvalidateFailed,
    NativeTimingRetrievalFailed,
    NativeTimingConversionFailed,
    DestinationMismatch,
    CopyCountMismatch,
    InstrumentationMismatch,
    Interrupted,
    UnclassifiedNativeFailure,
};

struct FailureClassification
{
    IntegrationStatus status{IntegrationStatus::Incomplete};
    IntegrationFailurePhase phase{
        IntegrationFailurePhase::ConfigurationValidation};
    IntegrationErrorCode errorCode{IntegrationErrorCode::InvalidConfiguration};
    bool safeForFurtherGpuCalls{};
    std::optional<std::int64_t> nativeErrorCode;
};

enum class HostCaptureStep
{
    Prepared,
    CaptureT0,
    Submit,
    CaptureT1,
    Wait,
    CaptureT2,
    PostCompletion,
};

struct CompletedValidation
{
    bool destinationLengthMatches{};
    bool destinationMatchesExpected{};
    bool sourceMatchesDeclaration{};
    bool sourcePreserved{};
    bool copyCountMatches{};
    bool executedCopyMatches{};
    bool hModeUninstrumented{};
    bool hostTimingValid{};
    bool vulkanResourceInvariants{true};

    [[nodiscard]] bool Passed() const noexcept;
};

struct BackendObservation
{
    bool nativeTransferCompleted{};
    bool destinationObserved{};
    bool cpuComparisonPerformed{};
    std::optional<bool> validationPassed;
    IntegrationStatus status{IntegrationStatus::Incomplete};
    std::optional<IntegrationFailurePhase> failurePhase;
    std::optional<IntegrationErrorCode> errorCode;
    std::optional<std::int64_t> nativeErrorCode;
    std::string nativeErrorName;
    std::string nativeOperation;
    bool safeForFurtherGpuCalls{true};
    TransferDirection direction{TransferDirection::HostToDevice};
    std::uint64_t expectedByteCount{};
    std::uint32_t expectedNativeCopyCount{};
    std::optional<std::uint32_t> completedNativeCopyCount;
    std::optional<bool> copyExecuted;
    bool hModeUninstrumented{};
    std::vector<std::uint8_t> destination;
    HostTimingIntervals hostTiming;
    bool hostCaptureComplete{};
};

struct CrossBackendObservation
{
    WorkloadConfiguration configuration;
    DeviceUuid verifiedDeviceUuid{};
    std::string verifiedDeviceUuidText;
    bool physicalIdentityVerified{};
    std::vector<std::uint8_t> source;
    std::vector<std::uint8_t> expectedDestination;
    std::string sourceSha256;
    std::string expectedDestinationSha256;
    BackendObservation cuda;
    BackendObservation vulkan;
    bool pairComparisonPerformed{};
    bool pairDestinationsEqual{};
    vulkan::Ex2VulkanEDiagnostics vulkanDiagnostics;

    [[nodiscard]] bool Passed() const noexcept;
};

enum class NativeTimingSmokeStatus
{
    NotApplicable,
    Valid,
    Failed,
};

struct NativeTimingSmokeBackendObservation
{
    BackendObservation transfer;
    NativeTimingSmokeStatus nativeTimingStatus{NativeTimingSmokeStatus::Failed};
    bool nativeTimingRetrieved{};
    bool nativeIntervalPresent{};
    bool nativeTimingMetadataValid{};
    std::string nativeTimingMethod;
};

struct NativeInstrumentationSmokeObservation
{
    WorkloadConfiguration configuration;
    DeviceUuid verifiedDeviceUuid{};
    std::string verifiedDeviceUuidText;
    bool physicalIdentityVerified{};
    NativeTimingSmokeBackendObservation cuda;
    NativeTimingSmokeBackendObservation vulkan;
    bool pairComparisonPerformed{};
    bool pairDestinationsEqual{};
    vulkan::Ex2VulkanEDiagnostics vulkanDiagnostics;

    [[nodiscard]] bool Passed() const noexcept;
};

enum class FailedSessionResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

[[nodiscard]] bool IsZeroDeviceUuid(const DeviceUuid& uuid) noexcept;
[[nodiscard]] std::string FormatDeviceUuid(const DeviceUuid& uuid);
[[nodiscard]] std::string VerifySamePhysicalDevice(
    const DeviceUuid& cudaUuid,
    const DeviceUuid& vulkanUuid);

void ValidateDeclaredSource(
    const WorkloadConfiguration& configuration,
    std::span<const std::uint8_t> source);

[[nodiscard]] bool IsValidHostCaptureOrder(
    std::span<const HostCaptureStep> steps) noexcept;

[[nodiscard]] CompletedValidation ValidateCompletedResult(
    std::span<const std::uint8_t> declaredSource,
    std::span<const std::uint8_t> originalSource,
    std::span<const std::uint8_t> expectedDestination,
    std::span<const std::uint8_t> observedDestination,
    std::uint32_t expectedNativeCopyCount,
    std::uint32_t completedNativeCopyCount,
    bool expectedCopyExecuted,
    bool copyExecuted,
    bool hModeUninstrumented,
    const HostTimingIntervals& hostTiming,
    bool vulkanResourceInvariants = true) noexcept;

void ReconcileCrossBackendDestinations(CrossBackendObservation& observation);

[[nodiscard]] BackendObservation MakeInterruptedObservation(
    TransferDirection direction,
    std::uint64_t byteCount,
    bool sharedDeviceKnownSafe) noexcept;

[[nodiscard]] FailedSessionResourceDisposition
ClassifyFailedSessionResourceDisposition(
    const BackendObservation& failedObservation) noexcept;

[[nodiscard]] FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaENativePhase phase,
    int nativeErrorCode) noexcept;
[[nodiscard]] FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanENativePhase phase,
    VkResult nativeResult) noexcept;

[[nodiscard]] CrossBackendObservation RunCrossBackendCorrectness(
    TransferDirection direction,
    std::uint64_t byteCount,
    int cudaDeviceOrdinal = 0,
    std::uint32_t vulkanPhysicalDeviceIndex = 0U);

[[nodiscard]] CrossBackendObservation
RunCrossBackendCorrectnessWithDeclaredData(
    TransferDirection direction,
    std::span<const std::uint8_t> source,
    std::span<const std::uint8_t> expectedDestination,
    int cudaDeviceOrdinal = 0,
    std::uint32_t vulkanPhysicalDeviceIndex = 0U);

[[nodiscard]] NativeInstrumentationSmokeObservation
RunNativeInstrumentationSmoke(
    TransferDirection direction,
    std::uint64_t byteCount,
    int cudaDeviceOrdinal = 0,
    std::uint32_t vulkanPhysicalDeviceIndex = 0U);

} // namespace computelab::ex2::e

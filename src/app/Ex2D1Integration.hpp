#pragma once

#include "cuda/Ex2CudaD1.hpp"
#include "ex2/Ex2Configuration.hpp"
#include "vulkan/Ex2VulkanD1.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace computelab::ex2::d1
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
    InitialUpload,
    UploadCompletion,
    StartMarker,
    SequenceSubmission,
    StopMarker,
    CommandBufferReset,
    CommandRecording,
    CompletionWait,
    FinalReadback,
    QueryReset,
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
    InitialUploadFailed,
    UploadCompletionFailed,
    StartMarkerFailed,
    SubmissionFailed,
    StopMarkerFailed,
    CommandBufferResetFailed,
    CommandRecordingFailed,
    CompletionFailed,
    OperationTimeout,
    DeviceLost,
    FinalReadbackFailed,
    QueryResetFailed,
    NativeTimingRetrievalFailed,
    NativeTimingConversionFailed,
    FinalStateMismatch,
    FinalBufferMismatch,
    DispatchCountMismatch,
    InterPassBarrierCountMismatch,
    NativeSequenceMismatch,
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
    std::optional<std::uint32_t> attemptedPass;
    std::optional<std::uint32_t> acceptedDispatchCount;
};

struct CompletedValidation
{
    bool finalStateLengthMatches{};
    bool finalStateMatchesExpected{};
    bool operationExpectedBufferMatches{};
    bool completedBufferMatches{};
    bool dispatchCountMatches{};
    bool interPassBarrierCountMatches{true};
    bool nativeSequenceMatches{};
    bool hModeUninstrumented{};

    [[nodiscard]] bool Passed() const noexcept;
};

struct BackendObservation
{
    bool nativeSequenceCompleted{};
    bool finalStateObserved{};
    bool cpuComparisonPerformed{};
    std::optional<bool> validationPassed;
    IntegrationStatus status{IntegrationStatus::Incomplete};
    std::optional<IntegrationFailurePhase> failurePhase;
    std::optional<IntegrationErrorCode> errorCode;
    bool safeForFurtherGpuCalls{true};
    bool nativeSequenceExecuted{};
    bool hModeUninstrumented{};
    IterativeFinalBuffer expectedFinalBuffer{IterativeFinalBuffer::StateA};
    std::optional<IterativeFinalBuffer> completedFinalBuffer;
    std::uint64_t expectedIterationCount{};
    std::optional<std::uint32_t> completedNativeDispatchCount;
    std::optional<std::uint32_t> completedInterPassBarrierCount;
    std::optional<std::uint32_t> attemptedPass;
    std::optional<std::uint32_t> acceptedDispatchCount;
    std::vector<std::uint32_t> finalState;
};

struct CrossBackendObservation
{
    WorkloadConfiguration configuration;
    DeviceUuid verifiedDeviceUuid{};
    std::string verifiedDeviceUuidText;
    bool physicalIdentityVerified{};
    std::vector<std::uint32_t> initialState;
    std::vector<std::uint32_t> expectedFinalState;
    IterativeFinalBuffer expectedFinalBuffer{IterativeFinalBuffer::StateA};
    std::string initialStateSha256;
    std::string expectedFinalStateSha256;
    std::string vulkanLoadedShaderName;
    std::string vulkanLoadedSpirvSha256;
    std::string vulkanShaderFileSha256;
    bool vulkanShaderProvenanceVerified{};
    BackendObservation cuda;
    BackendObservation vulkan;
    bool pairComparisonPerformed{};
    bool pairFinalStatesEqual{};
    vulkan::Ex2VulkanD1Diagnostics vulkanDiagnostics;

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

void ValidateDeclaredInitialState(
    const WorkloadConfiguration& configuration,
    std::span<const std::uint32_t> initialState);

[[nodiscard]] CompletedValidation ValidateCompletedResult(
    std::span<const std::uint32_t> expectedFinalState,
    std::span<const std::uint32_t> observedFinalState,
    IterativeFinalBuffer expectedFinalBuffer,
    IterativeFinalBuffer operationExpectedFinalBuffer,
    IterativeFinalBuffer completedFinalBuffer,
    std::uint64_t expectedIterationCount,
    std::uint32_t completedNativeDispatchCount,
    std::optional<std::uint32_t> expectedInterPassBarrierCount,
    std::optional<std::uint32_t> completedInterPassBarrierCount,
    bool nativeSequenceExecuted,
    bool hModeUninstrumented) noexcept;

[[nodiscard]] BackendObservation CompletedObservation(
    std::span<const std::uint32_t> expectedFinalState,
    std::vector<std::uint32_t> observedFinalState,
    IterativeFinalBuffer expectedFinalBuffer,
    IterativeFinalBuffer operationExpectedFinalBuffer,
    IterativeFinalBuffer completedFinalBuffer,
    std::uint64_t expectedIterationCount,
    std::uint32_t completedNativeDispatchCount,
    std::optional<std::uint32_t> expectedInterPassBarrierCount,
    std::optional<std::uint32_t> completedInterPassBarrierCount,
    bool nativeSequenceExecuted,
    bool hModeUninstrumented);

[[nodiscard]] BackendObservation MakeInterruptedObservation(
    bool sharedDeviceKnownSafe) noexcept;

void ReconcileCrossBackendFinalStates(CrossBackendObservation& observation);

[[nodiscard]] FailedSessionResourceDisposition
ClassifyFailedSessionResourceDisposition(
    const BackendObservation& failedObservation) noexcept;

[[nodiscard]] FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaD1NativePhase phase,
    int nativeErrorCode,
    std::optional<std::uint32_t> attemptedPass = std::nullopt,
    std::uint32_t acceptedDispatchCount = 0U) noexcept;
[[nodiscard]] FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanD1NativePhase phase,
    VkResult nativeResult) noexcept;

[[nodiscard]] CrossBackendObservation RunCrossBackendCorrectness(
    std::uint64_t elementCount,
    std::uint64_t iterationCount,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& d1SpirvPath);

} // namespace computelab::ex2::d1

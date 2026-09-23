#pragma once

#include "cuda/Ex2CudaC.hpp"
#include "ex2/Ex2Configuration.hpp"
#include "vulkan/Ex2VulkanC.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace computelab::ex2::c
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
    TargetUpload,
    UploadCompletion,
    CounterReset,
    ResetCompletion,
    Submission,
    CompletionWait,
    CounterReadback,
    TargetDiagnosticReadback,
    ValidationMismatch,
    InterruptedSecondBackend,
    NativeFailure,
};

enum class IntegrationErrorCode
{
    InvalidConfiguration,
    BackendInitializationFailed,
    ResourceAllocationFailed,
    TargetUploadFailed,
    UploadCompletionFailed,
    CounterResetFailed,
    ResetCompletionFailed,
    SubmissionFailed,
    CompletionFailed,
    OperationTimeout,
    DeviceLost,
    CounterReadbackFailed,
    TargetReadbackFailed,
    CounterMismatch,
    TargetPreservationFailed,
    NativeDispatchMismatch,
    Interrupted,
    UnclassifiedNativeFailure,
};

struct FailureClassification
{
    IntegrationStatus status{IntegrationStatus::Incomplete};
    IntegrationFailurePhase phase{
        IntegrationFailurePhase::ConfigurationValidation};
    IntegrationErrorCode errorCode{IntegrationErrorCode::InvalidConfiguration};
};

struct BufferValidation
{
    bool counterLengthMatches{};
    bool countersMatchExpected{};
    bool targetLengthMatches{};
    bool targetsPreserved{};
    bool counterTotalMatches{};
    bool nativeDispatchMatches{};

    [[nodiscard]] bool Passed() const noexcept;
};

struct BackendObservation
{
    bool nativeOperationCompleted{};
    bool countersObserved{};
    bool cpuComparisonPerformed{};
    std::optional<bool> validationPassed;
    IntegrationStatus status{IntegrationStatus::Incomplete};
    std::optional<IntegrationFailurePhase> failurePhase;
    std::optional<IntegrationErrorCode> errorCode;
    bool safeForFurtherGpuCalls{true};
    bool nativeDispatchExecuted{};
    std::vector<std::uint32_t> counters;
    std::vector<std::uint32_t> deviceTargets;
};

struct CrossBackendObservation
{
    WorkloadConfiguration configuration;
    DeviceUuid verifiedDeviceUuid{};
    std::string verifiedDeviceUuidText;
    bool physicalIdentityVerified{};
    std::vector<std::uint32_t> targets;
    std::vector<std::uint32_t> expectedCounters;
    std::string targetsSha256;
    std::string expectedCountersSha256;
    std::string vulkanLoadedShaderName;
    std::string vulkanLoadedSpirvSha256;
    std::string vulkanShaderFileSha256;
    bool vulkanShaderProvenanceVerified{};
    BackendObservation cuda;
    BackendObservation vulkan;
    bool pairComparisonPerformed{};
    bool pairCountersEqual{};
    vulkan::Ex2VulkanCDiagnostics vulkanDiagnostics;

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

void ValidateDeclaredTargets(
    const WorkloadConfiguration& configuration,
    std::span<const std::uint32_t> targets);

[[nodiscard]] BufferValidation ValidateBuffers(
    std::span<const std::uint32_t> expectedCounters,
    std::span<const std::uint32_t> observedCounters,
    std::span<const std::uint32_t> originalTargets,
    std::span<const std::uint32_t> observedDeviceTargets,
    bool nativeDispatchExecuted) noexcept;

[[nodiscard]] BackendObservation CompletedObservation(
    std::span<const std::uint32_t> expectedCounters,
    std::vector<std::uint32_t> observedCounters,
    std::span<const std::uint32_t> originalTargets,
    std::vector<std::uint32_t> observedDeviceTargets,
    bool nativeDispatchExecuted);

[[nodiscard]] BackendObservation MakeInterruptedObservation(
    bool sharedDeviceKnownSafe) noexcept;

void ReconcileCrossBackendCounters(CrossBackendObservation& observation);

[[nodiscard]] FailedSessionResourceDisposition
ClassifyFailedSessionResourceDisposition(
    const BackendObservation& failedObservation) noexcept;

[[nodiscard]] FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaCNativePhase phase,
    int nativeErrorCode) noexcept;
[[nodiscard]] FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanCNativePhase phase,
    VkResult nativeResult) noexcept;

[[nodiscard]] CrossBackendObservation RunCrossBackendCorrectness(
    std::uint64_t elementCount,
    std::uint64_t activeCounterCount,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& cSpirvPath);

} // namespace computelab::ex2::c

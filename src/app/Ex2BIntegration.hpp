#pragma once

#include "cuda/Ex2CudaB.hpp"
#include "ex2/Ex2Configuration.hpp"
#include "vulkan/Ex2VulkanB.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace computelab::ex2::b
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
    InputUpload,
    IndexUpload,
    OutputInitialization,
    UploadCompletion,
    Preparation,
    Submission,
    CompletionWait,
    OutputReadback,
    InputDiagnosticReadback,
    IndexDiagnosticReadback,
    ValidationMismatch,
    InterruptedSecondBackend,
    NativeFailure,
};

enum class IntegrationErrorCode
{
    InvalidConfiguration,
    BackendInitializationFailed,
    ResourceAllocationFailed,
    InputUploadFailed,
    IndexUploadFailed,
    OutputInitializationFailed,
    UploadCompletionFailed,
    PreparationFailed,
    SubmissionFailed,
    CompletionFailed,
    OperationTimeout,
    DeviceLost,
    OutputReadbackFailed,
    InputReadbackFailed,
    IndexReadbackFailed,
    OutputMismatch,
    PrimaryInputPreservationFailed,
    PermutationPreservationFailed,
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
    bool outputLengthMatches{};
    bool outputMatchesExpected{};
    bool primaryInputLengthMatches{};
    bool primaryInputPreserved{};
    bool permutationLengthMatches{};
    bool permutationPreserved{};

    [[nodiscard]] bool Passed() const noexcept;
};

struct BackendObservation
{
    bool nativeOperationCompleted{};
    bool outputObserved{};
    bool cpuComparisonPerformed{};
    std::optional<bool> validationPassed;
    IntegrationStatus status{IntegrationStatus::Incomplete};
    std::optional<IntegrationFailurePhase> failurePhase;
    std::optional<IntegrationErrorCode> errorCode;
    bool safeForFurtherGpuCalls{true};
    bool nativeDispatchExecuted{};
    std::vector<std::uint32_t> output;
    std::vector<std::uint32_t> devicePrimaryInput;
    std::vector<std::uint32_t> devicePermutation;
};

struct CrossBackendObservation
{
    WorkloadConfiguration configuration;
    DeviceUuid verifiedDeviceUuid{};
    std::string verifiedDeviceUuidText;
    bool physicalIdentityVerified{};
    std::vector<std::uint32_t> primaryInput;
    std::vector<std::uint32_t> permutation;
    std::vector<std::uint32_t> expectedOutput;
    std::string primaryInputSha256;
    std::string permutationSha256;
    std::string expectedOutputSha256;
    std::string vulkanLoadedShaderName;
    std::string vulkanLoadedSpirvSha256;
    std::string vulkanShaderFileSha256;
    bool vulkanShaderProvenanceVerified{};
    BackendObservation cuda;
    BackendObservation vulkan;
    bool pairComparisonPerformed{};
    bool pairOutputsEqual{};
    vulkan::Ex2VulkanBDiagnostics vulkanDiagnostics;

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

void ValidateDeclaredInputPair(
    const WorkloadConfiguration& configuration,
    std::span<const std::uint32_t> primaryInput,
    std::span<const std::uint32_t> permutation);

[[nodiscard]] BufferValidation ValidateBuffers(
    std::span<const std::uint32_t> expectedOutput,
    std::span<const std::uint32_t> observedOutput,
    std::span<const std::uint32_t> originalPrimaryInput,
    std::span<const std::uint32_t> observedDevicePrimaryInput,
    std::span<const std::uint32_t> originalPermutation,
    std::span<const std::uint32_t> observedDevicePermutation) noexcept;

[[nodiscard]] BackendObservation CompletedObservation(
    std::span<const std::uint32_t> expectedOutput,
    std::vector<std::uint32_t> observedOutput,
    std::span<const std::uint32_t> originalPrimaryInput,
    std::vector<std::uint32_t> observedDevicePrimaryInput,
    std::span<const std::uint32_t> originalPermutation,
    std::vector<std::uint32_t> observedDevicePermutation,
    bool nativeDispatchExecuted);

[[nodiscard]] BackendObservation MakeInterruptedObservation(
    bool sharedDeviceKnownSafe) noexcept;

// Reconciles only the pair-level property. Independent backend results are
// never rewritten when the pair disagrees.
void ReconcileCrossBackendOutputs(CrossBackendObservation& observation);

[[nodiscard]] FailedSessionResourceDisposition
ClassifyFailedSessionResourceDisposition(
    const BackendObservation& failedObservation) noexcept;

[[nodiscard]] FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaBNativePhase phase,
    int nativeErrorCode) noexcept;
[[nodiscard]] FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanBNativePhase phase,
    VkResult nativeResult) noexcept;

// CUDA executes first and Vulkan second. A CUDA failure or validation failure
// interrupts the Vulkan execution. Native failures whose completion is not
// known safe retain both native objects until process teardown.
[[nodiscard]] CrossBackendObservation RunCrossBackendCorrectness(
    IndexedVariant variant,
    std::uint64_t elementCount,
    IndexPattern pattern,
    std::uint64_t seed,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& b1SpirvPath,
    const std::filesystem::path& b2SpirvPath);

} // namespace computelab::ex2::b

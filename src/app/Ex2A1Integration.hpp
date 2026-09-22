#pragma once

#include "cuda/Ex2CudaA1.hpp"
#include "environment/EnvironmentCollector.hpp"
#include "ex2/Ex2Evidence.hpp"
#include "vulkan/Ex2VulkanA1.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace computelab::ex2::a1
{

using DeviceUuid = std::array<std::uint8_t, 16>;

struct FailureClassification
{
    evidence::OperationStatus status{evidence::OperationStatus::Incomplete};
    evidence::FailurePhase phase{evidence::FailurePhase::Interrupted};
    std::string errorCode{evidence::error_code::Interrupted};
};

struct BufferValidation
{
    bool outputLengthMatches{};
    bool outputMatchesExpected{};
    bool inputLengthMatches{};
    bool inputPreserved{};

    [[nodiscard]] bool Passed() const noexcept;
};

struct BackendObservation
{
    bool expectedOutputGenerated{true};
    bool operationCompleted{};
    bool outputObserved{};
    bool comparisonPerformed{};
    std::optional<bool> validationPassed;
    evidence::OperationStatus status{evidence::OperationStatus::Incomplete};
    std::optional<evidence::FailurePhase> failurePhase;
    std::optional<std::string> errorCode;
    bool safeForFurtherGpuCalls{true};
    bool nativeDispatchExecuted{};
    std::vector<std::uint32_t> output;
    std::vector<std::uint32_t> deviceInput;
};

struct CrossBackendObservation
{
    WorkloadConfiguration configuration;
    DeviceUuid verifiedDeviceUuid{};
    std::string verifiedDeviceUuidText;
    bool physicalIdentityVerified{};
    std::vector<std::uint32_t> input;
    std::vector<std::uint32_t> expectedOutput;
    std::string inputSha256;
    std::string expectedOutputSha256;
    std::string vulkanShaderSha256;
    BackendObservation cuda;
    BackendObservation vulkan;
    bool crossBackendComparisonPerformed{};
    bool crossBackendOutputsEqual{};
    vulkan::Ex2VulkanA1Diagnostics vulkanDiagnostics;

    [[nodiscard]] bool Passed() const noexcept;
};

enum class FailedSessionResourceDisposition
{
    DestroyNormally,
    PreserveForProcessTeardown,
};

struct EvidenceBuildContext
{
    std::string machineId;
    std::string sourceRevision;
    std::string executableSha256;
    std::string shaderSha256;
    std::string cudaRunId;
    std::string vulkanRunId;
    results::EnvironmentRecord cudaEnvironment;
    results::EnvironmentRecord vulkanEnvironment;
};

struct SerializedSeries
{
    evidence::EnvironmentRecord environment;
    std::vector<evidence::InitializationRecord> initialization;
    std::vector<evidence::SampleRecord> samples;
    evidence::SummaryRecord summary;
    std::string environmentJson;
    std::string initializationCsv;
    std::string samplesCsv;
    std::string summaryJson;
};

struct SerializedEvidencePair
{
    SerializedSeries cuda;
    SerializedSeries vulkan;
};

[[nodiscard]] bool IsZeroDeviceUuid(const DeviceUuid& uuid) noexcept;
[[nodiscard]] std::string FormatDeviceUuid(const DeviceUuid& uuid);
[[nodiscard]] std::string VerifySamePhysicalDevice(
    const DeviceUuid& cudaUuid,
    const DeviceUuid& vulkanUuid);

[[nodiscard]] BufferValidation ValidateBuffers(
    std::span<const std::uint32_t> expectedOutput,
    std::span<const std::uint32_t> observedOutput,
    std::span<const std::uint32_t> originalInput,
    std::span<const std::uint32_t> observedDeviceInput) noexcept;

[[nodiscard]] BackendObservation CompletedObservation(
    std::span<const std::uint32_t> expectedOutput,
    std::vector<std::uint32_t> observedOutput,
    std::span<const std::uint32_t> originalInput,
    std::vector<std::uint32_t> observedDeviceInput,
    bool nativeDispatchExecuted);

// Reconciles only the pair-level comparison. Backend status and validation
// remain the results of each backend's independent CPU-oracle and input checks.
void ReconcileCrossBackendOutputs(CrossBackendObservation& observation);

[[nodiscard]] FailedSessionResourceDisposition
ClassifyFailedSessionResourceDisposition(
    const BackendObservation& failedObservation) noexcept;

[[nodiscard]] FailureClassification ClassifyCudaFailure(
    cuda::Ex2CudaA1NativePhase phase,
    int nativeErrorCode) noexcept;
[[nodiscard]] FailureClassification ClassifyVulkanFailure(
    vulkan::Ex2VulkanA1NativePhase phase,
    VkResult nativeResult) noexcept;

// The policy is deliberately sequential: CUDA executes first, Vulkan second.
// A CUDA execution failure records Vulkan as interrupted and does not reorder or
// retry either backend. Construction and identity failures occur before a valid
// comparison identity exists and therefore throw without fabricating evidence.
[[nodiscard]] CrossBackendObservation RunCrossBackendCorrectness(
    std::uint64_t elementCount,
    std::uint64_t seed,
    int cudaDeviceOrdinal,
    std::uint32_t vulkanPhysicalDeviceIndex,
    const std::filesystem::path& spirvPath);

[[nodiscard]] SerializedEvidencePair BuildEvidence(
    const CrossBackendObservation& observation,
    EvidenceBuildContext context);

} // namespace computelab::ex2::a1

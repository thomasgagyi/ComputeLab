#pragma once

#include "ex2/Ex2Gate0.hpp"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::ex2::gate0::supervisor
{

inline constexpr std::uint32_t ManifestVersion = 1U;
inline constexpr std::uint32_t ExecutionRecordVersion = 1U;
inline constexpr std::uint64_t MaximumOperationTimeoutMilliseconds = 60'000U;
inline constexpr std::uint64_t MaximumChildTimeoutMilliseconds = 1'200'000U;
inline constexpr std::uint64_t MaximumCampaignTimeoutMilliseconds = 86'400'000U;
inline constexpr std::uint32_t ProgressMagic = 0x47303550U;
inline constexpr std::uint16_t ProgressVersion = 1U;

enum class ManifestType
{
    WarmupCharacterization,
    SampleCountQualification,
    InstrumentationControl,
};

enum class ProgressEventType : std::uint16_t
{
    OperationStarted = 1U,
    OperationCompleted = 2U,
};

struct ProgressEvent
{
    std::uint32_t magic{ProgressMagic};
    std::uint16_t version{ProgressVersion};
    ProgressEventType type{};
    std::uint64_t operationIndex{};
    std::int64_t performanceCounter{};
};

static_assert(sizeof(ProgressEvent) == 24U);

struct PrerequisiteReference
{
    std::string authorizationId;
    std::string evidenceId;
    std::string evidenceSha256;
    std::optional<std::uint64_t> selectedWarmupCount;
    std::optional<std::uint64_t> qualifiedSampleCount;
};

struct ManifestChild
{
    std::uint64_t sequenceIndex{};
    std::string pairId;
    Configuration configuration;
    std::string expectedGpuUuid;
    std::string expectedSourceRevision;
    std::string expectedExecutableSha256;
    std::optional<std::string> expectedShaderSha256;
    std::string declaredOutputPath;
};

struct QualificationManifest
{
    std::uint32_t manifestVersion{};
    ManifestType type{};
    std::string manifestId;
    std::string manifestSha256;
    std::string machineId;
    std::string protocolVersion;
    std::string childExecutablePath;
    std::string vulkanShaderPath;
    std::string supervisorRecordPath;
    bool continueAfterFatalFailure{};
    std::uint64_t operationTimeoutMilliseconds{};
    std::uint64_t childTimeoutMilliseconds{};
    std::uint64_t campaignTimeoutMilliseconds{};
    std::uint64_t declaredChildCount{};
    std::uint64_t declaredOperationCount{};
    std::optional<PrerequisiteReference> prerequisite;
    std::vector<ManifestChild> children;
};

struct PackageArtifact
{
    std::string relativePath;
    std::uint64_t sizeBytes{};
    std::string sha256;
};

struct PackageInspection
{
    std::string packageState{"missing"};
    bool structurallyValid{};
    bool admitted{};
    std::vector<std::string> integrityErrors;
    std::vector<std::string> admissionReasons;
    std::vector<PackageArtifact> artifacts;
};

struct ChildExecutionRecord
{
    std::uint64_t sequenceIndex{};
    std::string runId;
    std::string expectedOutputPath;
    std::string launchTimeUtc;
    std::string exitTimeUtc;
    std::optional<std::uint32_t> exitCode;
    std::optional<std::string> terminationReason;
    std::uint64_t operationStarts{};
    std::uint64_t operationCompletions{};
    std::string progressValidation{"not_started"};
    std::string pairingValidation{"preflight_passed"};
    PackageInspection package;
};

struct SupervisorExecutionRecord
{
    QualificationManifest manifest;
    std::string startTimeUtc;
    std::string endTimeUtc;
    std::vector<ChildExecutionRecord> actualExecutions;
    std::string overallExecutionStatus{"running"};
    std::optional<std::string> failureReason;
};

struct SupervisedProcessResult
{
    std::string launchTimeUtc;
    std::string exitTimeUtc;
    std::uint32_t exitCode{};
    std::optional<std::string> terminationReason;
    std::uint64_t operationStarts{};
    std::uint64_t operationCompletions{};
    std::string progressValidation;
};

[[nodiscard]] std::string_view ToString(ManifestType value) noexcept;
[[nodiscard]] QualificationManifest ParseManifest(
    std::string_view json,
    const std::filesystem::path& repositoryRoot);
[[nodiscard]] std::string CanonicalManifestPayload(
    std::string_view json);
[[nodiscard]] std::string CalculateManifestSha256(
    std::string_view json);
void ValidateManifest(const QualificationManifest& manifest);
[[nodiscard]] std::uint64_t ExpectedOperationCount(
    const ManifestChild& child);
[[nodiscard]] std::vector<std::string> ChildArguments(
    const ManifestChild& child);

[[nodiscard]] PackageInspection InspectPackage(
    const ManifestChild& child,
    std::optional<std::uint32_t> exitCode);
void ApplySupervisorAdmission(
    PackageInspection& package,
    const SupervisedProcessResult& execution,
    std::uint64_t expectedOperationCount);
[[nodiscard]] std::string_view PrerequisiteVerificationBoundary(
    const QualificationManifest& manifest) noexcept;
[[nodiscard]] std::string SerializeExecutionRecord(
    const SupervisorExecutionRecord& record);
void WriteExecutionRecord(
    const std::filesystem::path& path,
    const SupervisorExecutionRecord& record,
    bool complete);

[[nodiscard]] SupervisedProcessResult RunSupervisedProcess(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds operationTimeout,
    std::chrono::milliseconds childTimeout,
    std::chrono::steady_clock::time_point campaignDeadline);

class ProgressReporter final
{
public:
    ProgressReporter() = default;
    explicit ProgressReporter(std::uintptr_t nativeHandle);

    void OperationStarted(std::uint64_t index) const;
    void OperationCompleted(std::uint64_t index) const;
    [[nodiscard]] bool Enabled() const noexcept;

private:
    std::uintptr_t nativeHandle_{};
};

[[nodiscard]] std::optional<ProgressReporter> ExtractProgressReporter(
    std::vector<std::string_view>& arguments);

} // namespace computelab::ex2::gate0::supervisor

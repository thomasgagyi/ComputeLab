#pragma once

#include "ex2/Ex2CorrectnessProgress.hpp"
#include "ex2/Ex2Evidence.hpp"

#include <array>
#include <chrono>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::ex2::correctness::control
{

enum class ExitCode : int
{
    Success = 0,
    ConfigurationOrPreflightError = 2,
    CorrectnessFailure = 3,
    IncompleteOrTimeout = 4,
    EvidenceOrControlFailure = 5,
};

struct ManifestChild
{
    std::uint64_t sequenceIndex{};
    std::size_t coreCellIndex{};
    std::string sessionId;
    std::optional<std::string> vulkanShaderSha256;
};

struct Manifest
{
    std::string id;
    std::string sha256;
    std::string machineId;
    std::string childExecutablePath;
    std::string supervisorRecordPath;
    std::string sourceRevision;
    bool gitDirty{};
    std::string childExecutableSha256;
    std::string supervisorExecutableSha256;
    std::string gpuUuid;
    int cudaDeviceOrdinal{};
    std::uint32_t vulkanPhysicalDeviceIndex{};
    std::uint64_t operationTimeoutMs{};
    std::uint64_t childTimeoutMs{};
    std::uint64_t campaignTimeoutMs{};
    std::vector<ManifestChild> children;
    std::string canonicalJson;
};

struct RuntimePaths
{
    std::filesystem::path repositoryRoot;
    std::filesystem::path supervisorExecutable;
    // A1, A2, B1, B2, C, D1, in this order. E has no shader.
    std::array<std::filesystem::path, 6> shaders;
};

struct PreflightFacts
{
    std::string sourceRevision;
    bool gitDirty{};
    std::string childSha256;
    std::string supervisorSha256;
    std::string cudaUuid;
    std::string vulkanUuid;
    std::vector<std::optional<std::string>> shaderSha256;
};

[[nodiscard]] std::string CanonicalManifestPayload(std::string_view json);
[[nodiscard]] std::string CalculateManifestSha256(std::string_view json);
[[nodiscard]] Manifest ParseManifest(std::string_view json);
[[nodiscard]] std::vector<std::string> ChildArguments(
    const Manifest& manifest, const ManifestChild& child);
void ValidatePreflightFacts(const Manifest& manifest, const PreflightFacts& facts);
void RejectOutputCollisions(const Manifest& manifest,
    const std::filesystem::path& repositoryRoot);
[[nodiscard]] PreflightFacts CollectPreflightFacts(
    const Manifest& manifest, const RuntimePaths& paths);

struct ProgressState
{
    std::uint32_t eventCount{};
    std::int64_t lastCounter{};
    std::optional<std::int64_t> outstandingStart;
    std::optional<AttemptBackend> timedOutBackend;
    bool invalid{};
    void Accept(const ProgressEvent& event, std::int64_t now,
        std::int64_t timeoutTicks);
    [[nodiscard]] bool Full() const noexcept { return !invalid && eventCount == 4U; }
};

struct ProcessResult
{
    std::string launchTimeUtc;
    std::string exitTimeUtc;
    std::optional<std::uint32_t> exitCode;
    std::optional<std::string> terminationReason;
    std::string progressValidation;
    std::uint32_t eventCount{};
};

[[nodiscard]] ProcessResult RunSupervisedProcess(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    std::chrono::milliseconds operationTimeout,
    std::chrono::milliseconds childTimeout,
    std::chrono::steady_clock::time_point campaignDeadline);

class ProgressReporter
{
public:
    explicit ProgressReporter(std::uintptr_t handle = 0U);
    [[nodiscard]] AttemptObserver Observer() noexcept;
private:
    static void Publish(void* context, AttemptBackend backend,
        AttemptEvent event) noexcept;
    std::uintptr_t handle_{};
};
[[nodiscard]] ProgressReporter ExtractProgressReporter(
    std::vector<std::string_view>& arguments);

struct Artifact
{
    std::string relativePath;
    std::uint64_t sizeBytes{};
    std::string sha256;
};
struct PackageInspection
{
    std::string state{"missing"};
    bool structurallyValid{};
    bool bothCompleted{};
    std::optional<std::string> sidecarPhase;
    std::vector<std::string> errors;
    std::vector<Artifact> artifacts;
};
[[nodiscard]] PackageInspection InspectPackage(const Manifest& manifest,
    const ManifestChild& child, const std::filesystem::path& repositoryRoot,
    std::optional<std::uint32_t> exitCode);
[[nodiscard]] std::string ContinuationDecision(
    const ProcessResult& process, const PackageInspection& package);

struct ChildRecord
{
    ManifestChild child;
    ProcessResult process;
    PackageInspection package;
    std::string continuation{"launch_pending"};
};
struct ExecutionRecord
{
    Manifest manifest;
    PreflightFacts observed;
    std::string startTimeUtc;
    std::string endTimeUtc;
    std::vector<ChildRecord> executions;
    std::string status{"running"};
    std::optional<std::string> failureReason;
};
[[nodiscard]] std::string SerializeExecutionRecord(const ExecutionRecord& record);
void WriteExecutionRecord(const std::filesystem::path& path,
    const ExecutionRecord& record, bool complete, bool replaceOwnedIncomplete = false);
[[nodiscard]] ExitCode ExecuteManifest(const Manifest& manifest,
    const RuntimePaths& paths);

} // namespace computelab::ex2::correctness::control

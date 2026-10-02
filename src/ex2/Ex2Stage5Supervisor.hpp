#pragma once

#include "ex2/Ex2Stage5Progress.hpp"
#include "ex2/Ex2Stage5Evidence.hpp"
#include <chrono>
#include <filesystem>
#include <functional>

namespace computelab::ex2::stage5::control
{
enum class ExitCode : int
{
    CompletedQualificationAnalysis = 0, PreflightOrControlIncomplete = 2,
    ChildOrEvidenceIncomplete = 3, SupervisorPublicationFailure = 4,
};
struct ManifestChild
{
    std::uint64_t sequenceIndex{};
    std::size_t planIndex{};
    std::string sessionId;
};
struct ManifestGroup
{
    Stage5Phase phase{};
    std::vector<ManifestChild> children;
};
struct Manifest
{
    std::string id, sha256, machineId, childExecutablePath;
    std::string sourceRevision, childExecutableSha256, supervisorExecutableSha256, gpuUuid;
    std::string a1ShaderSha256, d1ShaderSha256;
    int cudaDeviceOrdinal{};
    std::uint32_t vulkanPhysicalDeviceIndex{};
    std::uint64_t operationTimeoutMs{}, childTimeoutMs{}, campaignTimeoutMs{};
    std::array<ManifestGroup, 3> groups;
    std::string canonicalJson;
};
struct RuntimePaths
{
    std::filesystem::path repositoryRoot, supervisorExecutable, a1Shader, d1Shader;
};
struct PreflightFacts
{
    std::string sourceRevision, branch;
    bool gitDirty{};
    std::string childSha256, supervisorSha256, cudaUuid, vulkanUuid, a1ShaderSha256, d1ShaderSha256;
};
[[nodiscard]] std::string CanonicalManifestPayload(std::string_view);
[[nodiscard]] std::string CalculateManifestSha256(std::string_view);
[[nodiscard]] Manifest ParseManifest(std::string_view);
[[nodiscard]] Manifest LoadManifestFromRepository(const std::filesystem::path& root,
    const std::filesystem::path& relativePath);
[[nodiscard]] std::string PhaseName(Stage5Phase);
[[nodiscard]] Condition FrozenCondition(Stage5Phase, std::optional<std::uint64_t> selectedW = {});
[[nodiscard]] std::filesystem::path ParseSupervisorArguments(std::span<const std::string_view>);
[[nodiscard]] std::filesystem::path ControlPath(const Manifest&, const std::filesystem::path& root);
[[nodiscard]] std::filesystem::path AnalysisPath(const Manifest&, const std::filesystem::path& root);
[[nodiscard]] std::vector<std::string> ChildArguments(const Manifest&, Stage5Phase,
    const ManifestChild&, std::optional<std::uint64_t> selectedW = {});
void ValidatePreflightFacts(const Manifest&, const PreflightFacts&);
void RejectOutputCollisions(const Manifest&, const std::filesystem::path& root);
[[nodiscard]] PreflightFacts CollectPreflightFacts(const Manifest&, const RuntimePaths&);

struct ProgressExpectation
{
    Stage5Phase phase{};
    std::size_t planIndex{};
    std::optional<std::uint64_t> selectedW;
};
class ProgressState final
{
public:
    explicit ProgressState(ProgressExpectation);
    void Accept(const ProgressEvent&, std::int64_t now, std::int64_t timeoutTicks);
    [[nodiscard]] bool Full() const noexcept;
    [[nodiscard]] std::uint32_t ExpectedEventCount() const noexcept;
    [[nodiscard]] AttemptIdentity NextIdentity() const;
    std::uint32_t eventCount{};
    std::int64_t lastCounter{};
    std::optional<std::int64_t> outstandingStart;
    std::optional<AttemptIdentity> activeAttempt, lastCompletedAttempt;
    bool invalid{}, operationTimedOut{};
private:
    ProgressExpectation expectation_;
};
struct ProcessResult
{
    std::string launchTimeUtc, exitTimeUtc;
    std::optional<std::uint32_t> processId, exitCode;
    std::optional<std::string> terminationReason;
    std::string progressValidation{"not_started"};
    std::uint32_t eventCount{};
    std::optional<AttemptIdentity> activeAttempt, lastCompletedAttempt;
};
[[nodiscard]] ProcessResult RunSupervisedProcess(const std::filesystem::path& executable,
    const std::vector<std::string>& arguments, const ProgressExpectation&,
    std::chrono::milliseconds operationTimeout, std::chrono::milliseconds childTimeout,
    std::chrono::steady_clock::time_point campaignDeadline);

struct Artifact { std::string relativePath; std::uint64_t sizeBytes{}; std::string sha256; };
struct PackageInspection
{
    std::string state{"missing"};
    bool structurallyValid{};
    std::vector<std::string> errors;
    std::vector<Artifact> artifacts;
    std::optional<ProcessInput> input;
    std::optional<std::string> failureContextJson;
};
// Reopens raw files; no dependency on app Bundle or child publication helpers.
[[nodiscard]] PackageInspection InspectPackage(const Manifest&, Stage5Phase, const ManifestChild&,
    const std::filesystem::path& root, std::optional<std::uint64_t> selectedW,
    std::optional<std::uint32_t> exitCode);
struct AnalysisArtifact { std::string relativePath, sha256; };
struct ChildRecord
{
    std::size_t groupIndex{};
    ManifestChild child;
    std::optional<std::uint64_t> selectedW;
    ProcessResult process;
    PackageInspection package;
    std::string continuation{"launch_pending"};
};
struct ExecutionRecord
{
    Manifest manifest;
    PreflightFacts observed;
    std::string startTimeUtc, endTimeUtc, status{"running"};
    bool campaignComplete{};
    std::optional<std::string> failureReason;
    std::vector<ChildRecord> executions;
    std::array<std::string, 3> groupStates{"not_reached", "not_reached", "conditional_not_reached"};
    std::array<std::optional<AnalysisArtifact>, 3> analyses;
    std::optional<AnalysisArtifact> campaignAnalysis;
    std::optional<std::uint64_t> selectedCommonW;
};
[[nodiscard]] std::string SerializeExecutionRecord(const ExecutionRecord&);
void WriteExecutionRecord(const std::filesystem::path&, const ExecutionRecord&,
    bool complete, bool replaceOwnedIncomplete = false);
[[nodiscard]] AnalysisArtifact PublishAnalysis(const std::filesystem::path& root,
    const std::filesystem::path& path, std::string_view bytes);
void RequireWarmupAnchor(const ExecutionRecord&, const std::filesystem::path& root);
[[nodiscard]] ExitCode ExecuteManifest(const Manifest&, const RuntimePaths&);

// Internal deterministic test seam; never manifest/CLI options or scientific overrides.
struct RuntimeServices
{
    std::function<PreflightFacts(const Manifest&, const RuntimePaths&)> collectFacts;
    std::function<ProcessResult(const std::filesystem::path&, const std::vector<std::string>&,
        const ProgressExpectation&, std::chrono::milliseconds, std::chrono::milliseconds,
        std::chrono::steady_clock::time_point)> runProcess;
    std::function<void(const ExecutionRecord&)> afterDurableUpdate;
};
[[nodiscard]] ExitCode ExecuteManifestWithServices(const Manifest&, const RuntimePaths&, const RuntimeServices&);
} // namespace computelab::ex2::stage5::control

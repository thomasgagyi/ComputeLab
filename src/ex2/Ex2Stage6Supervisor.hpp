#pragma once
#include "ex2/Ex2Stage6Progress.hpp"
#include "ex2/Ex2Stage6Analysis.hpp"
#include <filesystem>
#include <functional>

namespace computelab::ex2::stage6::control
{
inline constexpr std::uint32_t SupervisorForcedExitCode = 0xE6000002U;
inline constexpr std::uint64_t ContainmentGraceMs = 5000, DrainGraceMs = 5000;
enum class ExitCode : int { Completed = 0, ConfigurationOrPreflightRejected = 2, CampaignIncomplete = 3, ControlPublicationFailure = 4, InternalControlFailure = 5 };
struct ManifestChild { std::uint64_t sequenceIndex{}; std::string sessionId; };
struct ManifestGroup { std::uint64_t cellIndex{}; std::array<ManifestChild, ChildrenPerCell> children; };
struct Manifest
{
    std::string id, sha256, fileSha256, canonicalJson, machineId, childExecutablePath;
    std::string sourceRevision, childExecutableSha256, supervisorExecutableSha256, gpuUuid;
    std::array<std::string, 6> shaderSha256;
    int cudaDeviceOrdinal{}; std::uint32_t vulkanPhysicalDeviceIndex{};
    std::uint64_t operationTimeoutMs{}, childTimeoutMs{}, campaignTimeoutMs{};
    std::array<ManifestGroup, CoreCellCount> groups;
};
struct RuntimePaths
{
    std::filesystem::path repositoryRoot, supervisorExecutable;
    std::array<std::filesystem::path, 6> shaders;
};
struct PreflightFacts
{
    std::string sourceRevision; bool gitDirty{};
    std::string childSha256, supervisorSha256, cudaUuid, vulkanUuid;
    std::array<std::string, 6> shaderSha256;
};
[[nodiscard]] std::string CanonicalManifestPayload(std::string_view);
[[nodiscard]] std::string CalculateManifestSha256(std::string_view);
[[nodiscard]] Manifest ParseManifest(std::string_view);
[[nodiscard]] Manifest LoadManifestFromRepository(const std::filesystem::path&, const std::filesystem::path&);
[[nodiscard]] std::filesystem::path ParseSupervisorArguments(std::span<const std::string_view>);
[[nodiscard]] std::filesystem::path ControlPath(const Manifest&, const std::filesystem::path&);
[[nodiscard]] std::filesystem::path AnalysisPath(const Manifest&, const std::filesystem::path&);
[[nodiscard]] std::vector<std::string> ChildArguments(const Manifest&, std::uint64_t);
void ValidatePreflightFacts(const Manifest&, const PreflightFacts&);
void RejectOutputCollisions(const Manifest&, const std::filesystem::path&);
[[nodiscard]] PreflightFacts CollectPreflightFacts(const Manifest&, const RuntimePaths&);
[[nodiscard]] std::int64_t DeadlineTicks(std::uint64_t milliseconds, std::int64_t frequency);
[[nodiscard]] std::int64_t AddDeadline(std::int64_t start, std::int64_t ticks);
enum class ExitKind { NotAvailable, VoluntaryStage6, ProgressTransportAbort, SupervisorForced, Abnormal };
struct ProcessResult
{
    bool processCreated{}, containmentAssigned{}, containmentVerified{}, processResumed{};
    std::optional<std::uint32_t> processId, exitCode;
    std::string launchTimeUtc, exitTimeUtc;
    ExitKind exitKind{ExitKind::NotAvailable};
    progress::TerminalForm progressForm{progress::TerminalForm::NotStarted};
    std::vector<progress::Observation> observations;
    std::optional<execution::AttemptIdentity> activeAttempt, lastReturnedAttempt;
    bool progressInvalid{}, progressTransportFailed{}, cleanEof{};
    std::uint32_t trailingBytes{};
    bool operationTimedOut{}, childTimedOut{}, campaignTimedOut{};
    std::optional<std::uint64_t> timedOutSampleIndex;
    bool supervisorTerminationRequested{}, terminateJobSucceeded{}, primaryTerminationConfirmed{};
    std::optional<std::uint32_t> jobTotalProcesses, jobActiveProcesses;
    bool descendantSurvivalObserved{}, jobEmptyConfirmed{}, containmentVerificationFailed{};
    std::optional<std::string> controlError;
};
struct ProcessTestFaults
{
    bool finalAccountingUnavailableAfterSurvival{};
    // Test-only override, after primary handle release and before any forced cleanup.
    std::function<void(std::uint32_t&, std::uint32_t&)> naturalAccountingSample;
};
[[nodiscard]] ProcessResult RunSupervisedProcess(const std::filesystem::path& executable,
    const std::vector<std::string>& arguments, std::uint64_t slot,
    std::uint64_t operationTimeoutMs, std::uint64_t childTimeoutMs,
    std::int64_t qpcFrequency, std::int64_t campaignDeadline, ProcessTestFaults = {});
enum class Topology { Missing, SidecarOnly, StagingOnly, StagingWithSidecar, FinalOnly, FinalWithSidecar, Contradictory };
enum class PackageLocation { None, Final, Staging };
struct Artifact
{
    std::string name, relativePath; std::uint64_t sizeBytes{}; std::string sha256;
    bool operator==(const Artifact&) const = default;
};
struct Sidecar
{
    std::uint64_t slotSequenceIndex{}, exitCategory{};
    std::string sessionId, runId, seriesId, sourceRevision, failurePhase, errorCode;
    bool foundationEstablished{}, completionUncertain{};
    std::optional<std::uint64_t> sampleIndex;
    std::optional<std::string> nativePhase, nativeCode, nativeDetail;
};
struct PackageInspection
{
    bool attempted{}; Topology topology{Topology::Missing};
    bool structurallyValid{}, packageFinalized{}, exactFileSet{}, scientificBundleParsed{}, scientificBundleValid{}, scientificBytesCanonical{}, changedDuringInspection{};
    PackageLocation location{PackageLocation::None};
    std::vector<Artifact> artifacts;
    std::optional<std::string> packageSha256;
    std::optional<analysis::ProcessInput> input;
    bool sidecarPresent{}, sidecarValid{};
    std::optional<Artifact> sidecarArtifact;
    std::optional<Sidecar> sidecar;
    std::vector<std::string> errors;
};
[[nodiscard]] evidence::Foundation ReconstructFoundation(const Manifest&, std::uint64_t);
[[nodiscard]] std::string PackageHash(std::span<const Artifact>);
// Disposable smoke can retain truthful dirty-source metadata; Release/H checks
// remain mandatory. Production always uses SupervisedCampaign.
enum class InspectionPurpose { SupervisedCampaign, DisposableSmoke };
struct InspectionTestHooks { std::function<void()> beforeSecondPass; };
[[nodiscard]] PackageInspection InspectPackage(const Manifest&, std::uint64_t,
    const std::filesystem::path&, InspectionPurpose = InspectionPurpose::SupervisedCampaign, const InspectionTestHooks& = {});
struct Reconciliation
{
    analysis::SlotDisposition disposition{analysis::SlotDisposition::UnresolvedCampaignFatal};
    std::string reason;
    std::optional<analysis::ProcessDescription> process;
};
[[nodiscard]] Reconciliation ReconcileSlot(const ProcessResult&, const PackageInspection&);
struct SlotRecord
{
    std::string phase{"pending"};
    std::optional<std::uint64_t> enteredRevision, processRevision, inspectionRevision, terminalRevision;
    std::optional<analysis::SlotDisposition> disposition;
    ProcessResult process; PackageInspection inspection;
    std::string reason, continuation{"pending"};
};
struct Ledger
{
    Manifest manifest; PreflightFacts observed;
    std::uint64_t revision{};
    std::string state{"owned"}, controlStartUtc, campaignStartUtc, campaignEndUtc;
    std::optional<std::int64_t> qpcFrequency, campaignStartQpc, campaignDeadlineQpc;
    std::optional<std::uint64_t> fatalSequence;
    std::optional<std::string> fatalReason;
    std::array<SlotRecord, TotalChildCount> slots;
    std::array<std::optional<Artifact>, CoreCellCount> cellAnalyses;
    std::optional<Artifact> campaignAnalysis;
};
[[nodiscard]] std::string SerializeLedger(const Ledger&);
void ValidateLedgerBytes(std::string_view);
[[nodiscard]] Artifact PublishAnalysis(const std::filesystem::path& root,
    const std::filesystem::path& finalPath, std::string_view bytes);
// This seam is for deterministic tests only. The executable always supplies the
// real services; the immutable production schedule is never replaceable.
struct RuntimeServices
{
    std::function<PreflightFacts(const Manifest&, const RuntimePaths&)> collectFacts;
    std::function<ProcessResult(const Manifest&, const RuntimePaths&, std::uint64_t, std::int64_t, std::int64_t)> runProcess;
    std::function<PackageInspection(const Manifest&, std::uint64_t, const std::filesystem::path&)> inspectPackage;
    std::function<Artifact(const std::filesystem::path&, const std::filesystem::path&, std::string_view)> publishAnalysis;
    std::function<void(const Ledger&)> afterDurableUpdate;
    std::function<void(std::string_view, const Ledger&)> publicationFault;
};
[[nodiscard]] ExitCode ExecuteManifestWithServices(const Manifest&, const RuntimePaths&, const RuntimeServices&);
[[nodiscard]] ExitCode ExecuteManifest(const Manifest&, const RuntimePaths&);
} // namespace computelab::ex2::stage6::control

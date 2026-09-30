#pragma once

#include "ex2/Ex2CorrectnessFoundation.hpp"
#include "app/Ex2A1Integration.hpp"
#include "app/Ex2BIntegration.hpp"
#include "app/Ex2CIntegration.hpp"
#include "app/Ex2D1Integration.hpp"
#include "app/Ex2EIntegration.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace computelab::ex2::correctness
{

enum class ExitCode : int
{
    Completed = 0,
    PreFoundationFailure = 2,
    ExecutionFailure = 3,
    PackageFailure = 4,
};

enum class CoreCellRoute
{
    A1,
    A2,
    B1,
    B2,
    C,
    D1,
    E1,
    E2,
};

struct CoreCellSelection
{
    std::size_t index{};
    CoreCellRoute route{};
    WorkloadConfiguration configuration;
};

[[nodiscard]] CoreCellSelection SelectApprovedCoreCell(std::size_t index);
void RequireApprovedCoreCell(const WorkloadConfiguration& configuration);

enum class PostFoundationFailureStage
{
    BackendInitialization,
    ResourceAllocation,
    Submission,
    Completion,
    Readback,
    Interrupted,
};

struct PostFoundationFailureFacts
{
    PostFoundationFailureStage stage{};
    bool timeout{};
    bool deviceLost{};
    bool operationCompleted{};
    bool outputObserved{};
};

[[nodiscard]] bool HasEstablishedSetup(const a1::BackendObservation& observation);
[[nodiscard]] bool HasEstablishedSetup(const b::BackendObservation& observation);
[[nodiscard]] bool HasEstablishedSetup(const c::BackendObservation& observation);
[[nodiscard]] bool HasEstablishedSetup(const d1::BackendObservation& observation);
[[nodiscard]] bool HasEstablishedSetup(const e::BackendObservation& observation);

[[nodiscard]] evidence::I7FailureKind MapPostFoundationFailure(
    const PostFoundationFailureFacts& facts);

struct CorrectnessChildConfiguration
{
    std::size_t coreCellIndex{};
    int cudaDeviceOrdinal{};
    std::uint32_t vulkanPhysicalDeviceIndex{};
    std::string machineId;
    std::string sessionId;
};

struct CorrectnessRuntimePaths
{
    std::filesystem::path repositoryRoot;
    std::filesystem::path localResultsRoot;
    std::filesystem::path executable;
    std::filesystem::path a1Spirv;
    std::filesystem::path a2Spirv;
    std::filesystem::path b1Spirv;
    std::filesystem::path b2Spirv;
    std::filesystem::path cSpirv;
    std::filesystem::path d1Spirv;
};

struct SessionPlan
{
    std::string sessionId;
    std::string cudaRunId;
    std::string vulkanRunId;
    std::filesystem::path localResultsRoot;
    std::filesystem::path stagingDirectory;
    std::filesystem::path finalDirectory;
    std::filesystem::path failureSidecar;
    std::filesystem::path cudaDirectory;
    std::filesystem::path vulkanDirectory;
};

[[nodiscard]] SessionPlan MakeSessionPlan(
    const std::filesystem::path& localResultsRoot,
    std::string sessionId);
void RequireUnusedSessionPlan(const SessionPlan& plan);

struct WordLogicalData
{
    std::vector<std::uint32_t> input;
    std::vector<std::uint32_t> expected;
};

struct IndexedLogicalData
{
    std::vector<std::uint32_t> primary;
    std::vector<std::uint32_t> permutation;
    std::vector<std::uint32_t> expected;
};

struct ContentionLogicalData
{
    std::vector<std::uint32_t> targets;
    std::vector<std::uint32_t> initialCounters;
    std::vector<std::uint32_t> expected;
};

struct ByteLogicalData
{
    std::vector<std::uint8_t> source;
    std::vector<std::uint8_t> expected;
};

using OwnedLogicalData = std::variant<
    WordLogicalData,
    IndexedLogicalData,
    ContentionLogicalData,
    ByteLogicalData>;

struct PreparedFoundationPair
{
    CoreCellSelection cell;
    OwnedLogicalData logicalData;
    evidence::I7CorrectnessFoundation cuda;
    evidence::I7CorrectnessFoundation vulkan;
};

[[nodiscard]] PreparedFoundationPair PrepareFoundationPair(
    CoreCellSelection cell,
    std::string machineId,
    std::string verifiedGpuUuid,
    std::string sourceRevision,
    std::string executableSha256,
    std::optional<std::string> vulkanShaderSha256,
    std::string cudaRunId,
    std::string vulkanRunId);

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

struct SerializedPair
{
    SerializedSeries cuda;
    SerializedSeries vulkan;
};

[[nodiscard]] SerializedSeries BuildSerializedSeries(
    const evidence::I7CorrectnessFoundation& foundation,
    results::EnvironmentRecord commonEnvironment,
    evidence::SampleRecord sample,
    bool setupEstablished,
    evidence::BackendDiagnostics diagnostics,
    std::string setupObservation);

struct DiskArtifact
{
    std::string relativePath;
    std::uint64_t sizeBytes{};
    std::string sha256;
};

struct DiskVerification
{
    std::vector<DiskArtifact> artifacts;
};

void CreateStagingSession(const SessionPlan& plan);
void WriteStagedSession(const SessionPlan& plan, const SerializedPair& pair);
[[nodiscard]] DiskVerification VerifyStagedSession(
    const SessionPlan& plan,
    const SerializedPair& expected);
void FinalizeStagedSession(const SessionPlan& plan);

enum class ExternalFailurePhase
{
    Configuration,
    Provenance,
    InputGeneration,
    DeviceIdentity,
    BackendExecution,
    Validation,
    EvidenceSerialization,
    EvidencePublication,
    PackageVerification,
    Interrupted,
};

struct FailureDisposition
{
    ExternalFailurePhase phase;
    std::string_view errorCode;
    ExitCode exitCode;
};

[[nodiscard]] FailureDisposition StagingCreationFailure() noexcept;
[[nodiscard]] FailureDisposition ClassifyExecutionFailure(
    bool completedObservations) noexcept;
[[nodiscard]] bool IsStagingRetained(const SessionPlan& plan) noexcept;

struct ExternalFailureRecord
{
    std::uint32_t recordVersion{1U};
    std::string sessionId;
    ExternalFailurePhase phase{ExternalFailurePhase::Configuration};
    std::string errorCode;
    bool foundationEstablished{};
    std::optional<std::string> sourceRevision;
    std::optional<std::string> cudaRunId;
    std::optional<std::string> vulkanRunId;
    bool stagingRetained{};
    std::optional<std::string> detail;
};

[[nodiscard]] std::string_view ToString(ExternalFailurePhase phase) noexcept;
[[nodiscard]] std::string SerializeExternalFailureRecord(
    const ExternalFailureRecord& record);
void WriteExternalFailureRecord(
    const SessionPlan& plan,
    const ExternalFailureRecord& record);

[[nodiscard]] ExitCode RunCorrectnessChild(
    const CorrectnessChildConfiguration& configuration,
    const CorrectnessRuntimePaths& paths,
    control::AttemptObserver observer = {});

} // namespace computelab::ex2::correctness

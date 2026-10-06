#pragma once

#include "ex2/Ex2Stage6Evidence.hpp"
#include "ex2/Ex2HostTiming.hpp"
#include <array>
#include <filesystem>
#include <functional>
#include <map>

namespace computelab::ex2::stage6::execution
{
namespace ev = computelab::ex2::stage6::evidence;
struct Configuration
{
    std::size_t cellIndex{}, planIndex{};
    int cudaDevice{};
    std::uint32_t vulkanDevice{};
    std::string machineId, sessionId, expectedGpuUuid;
};
struct RuntimePaths
{
    std::filesystem::path repositoryRoot, executable;
    std::filesystem::path a1Shader, a2Shader, b1Shader, b2Shader, cShader, d1Shader;
};
[[nodiscard]] Configuration ParseConfiguration(std::span<const std::string_view>);
[[nodiscard]] PlannedSlot ResolveSlot(const Configuration&);
[[nodiscard]] std::optional<std::filesystem::path> ApplicableShader(const PlannedSlot&, const RuntimePaths&);
struct Provenance
{
    std::string sourceRevision;
    bool dirty{};
    std::string executableSha256;
    std::optional<std::string> shaderSha256;
    bool operator==(const Provenance&) const = default;
};
[[nodiscard]] Provenance ResolveProvenance(const RuntimePaths&, const PlannedSlot&);
void RequireSameProvenance(const Provenance&, const Provenance&);
[[nodiscard]] std::string FormatUuid(const std::array<std::uint8_t, 16>&);
void RequireGpuIdentity(std::string_view expected, std::string_view preflight, std::string_view native);
void RequireLoadedShader(const ev::Foundation&, std::span<const std::uint32_t>, std::optional<std::string_view> name = {});

struct AttemptIdentity
{
    std::uint64_t slotSequenceIndex{}, sampleIndex{};
    bool operator==(const AttemptIdentity&) const = default;
};
enum class AttemptEvent : std::uint32_t { Started = 1, Returned = 2 };
struct AttemptObserver
{
    void* context{};
    void (*callback)(void*, const AttemptIdentity&, AttemptEvent) noexcept{};
    void Report(const AttemptIdentity&, AttemptEvent) const noexcept;
};
enum class ExitCode : int
{
    Completed = 0, PreFoundationFailure = 2, NativeStateUncertain = 3,
    PublicationFailure = 4, InternalFailure = 5
};
[[nodiscard]] ExitCode DominantExit(ExitCode, ExitCode) noexcept;
// Control facts only. H captures and correctness progress belong to the attempt,
// and are mapped directly into the sole scientific SampleRecord.
struct FailureContext
{
    ev::FailurePhase phase{ev::FailurePhase::BackendInitialization};
    bool timeout{}, deviceLost{}, allocation{}, completionUncertain{};
    std::optional<std::string> nativePhase, nativeCode, nativeDetail;
};
// Called only after a representable sequence returns; throwing attempts retain attribution.
void CompleteSampleSequence(std::optional<std::uint64_t>& currentSample,
    const std::optional<FailureContext>& retainedNativeFailure) noexcept;
[[nodiscard]] FailureContext CudaFailureContext(ev::FailurePhase, std::string_view nativePhase,
    int nativeCode, std::string_view nativeName);
[[nodiscard]] FailureContext VulkanFailureContext(ev::FailurePhase, std::string_view nativePhase,
    int nativeCode, std::string_view nativeOperation);
[[nodiscard]] ev::SampleRecord MapAttempt(const ev::Foundation&, std::uint64_t,
    const ev::CorrectnessProgress&, const HostTimingIntervals&, const std::optional<FailureContext>&);
// Narrow sequencing seam shared by real route-local lambdas and pure tests.
void RetainObservedAttempt(const AttemptIdentity&, const AttemptObserver&,
    const std::function<ev::SampleRecord()>&, std::vector<ev::SampleRecord>&);

struct SessionPaths
{
    std::filesystem::path finalDirectory, stagingDirectory, failureSidecar;
};
[[nodiscard]] SessionPaths MakeSessionPaths(const RuntimePaths&, std::string_view session);
void RequireUnusedSession(const SessionPaths&);
void CreateStaging(const SessionPaths&);
using Artifacts = std::map<std::string, std::string>;
[[nodiscard]] Artifacts SerializePackage(const ev::Foundation&, const ev::EnvironmentRecord&,
    std::span<const ev::InitializationRecord>, std::span<const ev::SampleRecord>, const ev::SummaryRecord&);
void WriteStagedPackage(const SessionPaths&, const Artifacts&);
void VerifyStagedPackage(const SessionPaths&, const Artifacts&);
void FinalizePackage(const SessionPaths&);
struct FailureSidecar
{
    std::string sessionId;
    std::optional<std::uint64_t> slotSequenceIndex;
    bool foundationEstablished{};
    std::optional<std::string> runId, seriesId, sourceRevision;
    ExitCode exitCategory{ExitCode::InternalFailure};
    std::string failurePhase, errorCode;
    std::optional<std::uint64_t> sampleIndex;
    bool completionUncertain{};
    std::optional<std::string> nativePhase, nativeCode, nativeDetail;
};
[[nodiscard]] std::string SerializeFailureSidecar(const FailureSidecar&);
void WriteFailureSidecar(const SessionPaths&, const FailureSidecar&);
// Terminal publication only; the callback performs the provenance recheck.
[[nodiscard]] ExitCode PublishPreparedPackage(const SessionPaths&, const Artifacts&,
    FailureSidecar, bool sidecarRequired, ExitCode, const std::function<void()>&);
[[nodiscard]] ExitCode RunChild(const Configuration&, const RuntimePaths&, AttemptObserver = {});
} // namespace computelab::ex2::stage6::execution

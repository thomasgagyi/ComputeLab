#pragma once

#include "ex2/Ex2Stage5Evidence.hpp"
#include "ex2/Ex2HostTiming.hpp"

#include <filesystem>

namespace computelab::ex2::stage5::execution
{
namespace ev = stage5::evidence;

enum class ExitCode { Completed = 0, PreflightFailure = 2, ExecutionFailure = 3, PublicationFailure = 4 };
struct Configuration
{
    Stage5Phase phase{};
    std::size_t planIndex{};
    std::optional<std::uint64_t> selectedW;
    int cudaDevice{};
    std::uint32_t vulkanDevice{};
    std::string machineId, sessionId, expectedGpuUuid;
};
struct Route { Condition condition; PlannedProcess process; std::uint64_t preparationCount{}; };
[[nodiscard]] Configuration ParseArguments(std::span<const std::string_view>);
[[nodiscard]] Route ResolveRoute(const Configuration&);
[[nodiscard]] std::string FormatUuid(const std::array<std::uint8_t, 16>&);
void RequireExpectedUuid(std::string_view, const std::array<std::uint8_t, 16>&);
void ValidateProvenance(const Route&, std::string_view sourceRevision, bool dirty,
    std::string_view executableHash, const std::optional<std::string>& shaderHash);

// These seams consume retained captures or already completed native facts;
// they never provide a substitute launch/wait/readback implementation.
[[nodiscard]] std::vector<ev::HostClockRecord> ClockRecords(const ev::Foundation&,
    std::span<const std::optional<timing::HostTimePoint>>);
struct Attempt
{
    std::optional<timing::HostTimePoint> t0, t1, t2;
    bool waitSucceeded{};
    std::optional<std::vector<std::uint32_t>> output;
    bool executed{};
    std::uint32_t dispatchCount{}, barrierCount{};
    std::optional<IterativeFinalBuffer> finalBuffer;
    ev::Status status{ev::Status::Ok};
    std::optional<ev::FailurePhase> failurePhase;
    std::optional<std::string> errorCode;
    std::optional<std::string> nativePhase, nativeDetail;
    std::optional<std::int64_t> nativeCode;
    bool completionUncertain{};
};
// A successful wait with invalid timestamps has no representable schema-v2
// completed row. Preserve that attempt in control context rather than invent t2.
struct MappedAttempt
{
    std::optional<ev::OperationRecord> record;
    bool successful{};
    std::string errorCode;
    std::optional<bool> validationPassed;
};
[[nodiscard]] MappedAttempt MapAttempt(const ev::Foundation&, std::uint64_t index,
    const Attempt&, std::span<const std::uint32_t> expected);

struct SessionPaths { std::filesystem::path staging, final, failure; };
[[nodiscard]] SessionPaths MakeSessionPaths(const std::filesystem::path& localRoot, std::string_view session);
void RequireUnused(const SessionPaths&);
void CreateStaging(const SessionPaths&);
struct Bundle
{
    ev::Foundation foundation;
    ev::EnvironmentRecord environment;
    std::vector<ev::InitializationRecord> initialization;
    std::vector<ev::HostClockRecord> clock;
    std::vector<ev::WarmupRecord> warmup;
    std::vector<ev::SampleRecord> samples;
};
// All complete-package paths regenerate the summary and validate the bundle.
void WriteCompleteBundle(const SessionPaths&, const Bundle&);
void VerifyStagedBundle(const SessionPaths&, const Bundle&);
void FinalizeBundle(const SessionPaths&, const Bundle&);

struct FailureRecord
{
    std::string sessionId;
    std::optional<Stage5Phase> phase;
    std::optional<std::size_t> planIndex;
    std::optional<PlannedProcess> process;
    ExitCode category{ExitCode::PreflightFailure};
    std::string failurePhase, errorCode, detail;
    bool foundationEstablished{}, stagingRetained{}, completionUncertain{};
    std::optional<std::string> sourceRevision, runId, seriesId, nativePhase, nativeDetail;
    std::optional<std::int64_t> nativeCode;
    std::optional<std::uint64_t> attemptIndex;
    bool preparationAttempt{};
    std::optional<bool> successfulWait;
    std::optional<bool> validationPassed;
    std::optional<std::uint64_t> hostSubmissionNanoseconds, hostWaitNanoseconds, hostCompletionNanoseconds;
};
// Set current control context before A1 preparation can fail. No evidence row.
void BeginA1Observation(FailureRecord&, std::uint64_t index);
// Testable record/file sink for a single already-completed attempt. Preparation
// attempts are checked but never appended to either diagnostic or sample CSV.
[[nodiscard]] bool RetainAttempt(Bundle&, const SessionPaths&, FailureRecord&,
    std::span<const std::uint32_t> expected, const Attempt&, std::uint64_t index, bool preparation);
[[nodiscard]] std::string SerializeFailure(const FailureRecord&);
void WriteFailure(const SessionPaths&, const FailureRecord&);
struct RuntimePaths { std::filesystem::path repository, executable, a1Shader, d1Shader; };
[[nodiscard]] ExitCode RunChild(const Configuration&, const RuntimePaths&);
} // namespace computelab::ex2::stage5::execution

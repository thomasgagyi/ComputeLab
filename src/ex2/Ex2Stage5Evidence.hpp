#pragma once

#include "ex2/Ex2Evidence.hpp"
#include "ex2/Ex2Stage5Qualification.hpp"

namespace computelab::ex2::stage5::evidence
{

// Reuse only unchanged record vocabulary, not correctness/Gate-0 validators.
using Status = computelab::ex2::evidence::OperationStatus;
using FailurePhase = computelab::ex2::evidence::FailurePhase;
using CorrectnessProgress = computelab::ex2::evidence::CorrectnessProgress;
using BackendDiagnostics = computelab::ex2::evidence::BackendDiagnostics;
using InitializationRecord = computelab::ex2::evidence::InitializationRecord;

struct FoundationRequest
{
    Condition condition;
    PlannedProcess process;
    std::string runId;
    std::string machineId;
    SuppliedGpuIdentity gpuIdentity;
    std::string sourceRevision;
    std::string executableSha256;
    std::optional<std::string> shaderSha256;
};

struct Plan
{
    Condition condition;
    PlannedProcess process;
    std::string runId;
    SeriesIdentityContext seriesIdentity;
    std::string comparisonConditionId;
    std::string seriesId;
};

class Foundation final
{
public:
    [[nodiscard]] const Plan& Identity() const noexcept { return plan_; }
    [[nodiscard]] const std::string& InputSha256() const noexcept { return inputSha256_; }
    [[nodiscard]] const std::string& ExpectedOutputSha256() const noexcept { return expectedOutputSha256_; }
private:
    Foundation(Plan plan, std::string inputSha256, std::string expectedOutputSha256);
    Plan plan_;
    std::string inputSha256_, expectedOutputSha256_;
    friend Foundation MakeWordFoundation(const FoundationRequest&,
        std::span<const std::uint32_t>, std::span<const std::uint32_t>);
};

// Pre-foundation failures throw; no plan-bound record can be constructed from
// fabricated digest text. CPU input/oracle checks occur only outside operations.
[[nodiscard]] WorkloadConfiguration WorkloadFor(const Condition& condition);
[[nodiscard]] Foundation MakeWordFoundation(const FoundationRequest& request,
    std::span<const std::uint32_t> input, std::span<const std::uint32_t> expected);
void ValidatePlan(const Plan& plan);

struct EnvironmentRecord
{
    results::EnvironmentRecord common;
    Plan plan;
    std::string inputSha256, expectedOutputSha256;
    BackendDiagnostics backendDiagnostics;
    std::string evidenceKind{EvidenceKind};
};

struct HostClockRecord
{
    std::string runId, seriesId;
    std::uint64_t processIndex{}, sequenceIndex{};
    std::optional<std::uint64_t> deltaNanoseconds;
};

struct OperationRecord
{
    Plan plan;
    std::uint64_t sequenceIndex{};
    CorrectnessProgress correctness;
    Status status{Status::Incomplete};
    std::optional<FailurePhase> failurePhase;
    std::optional<std::string> errorCode;
    std::optional<std::uint64_t> hostSubmissionNanoseconds;
    std::optional<std::uint64_t> hostWaitNanoseconds;
    std::optional<std::uint64_t> hostCompletionNanoseconds;
    std::optional<std::uint64_t> nativeDeviceIntervalNanoseconds;
};
// Same operation facts, different artifact contracts. No phase CSV column.
struct WarmupRecord : OperationRecord {};
struct SampleRecord : OperationRecord {};

struct SummaryRecord
{
    Plan plan;
    Status processStatus{Status::Incomplete};
    std::optional<FailurePhase> failurePhase;
    std::optional<std::string> errorCode;
    std::uint64_t recordedSampleCount{}, validationFailures{}, failedSampleCount{};
    std::optional<results::MetricSummary> hostSubmissionNanoseconds;
    std::optional<results::MetricSummary> hostWaitNanoseconds;
    std::optional<results::MetricSummary> hostCompletionNanoseconds;
    std::optional<results::MetricSummary> nativeDeviceIntervalNanoseconds;
};

[[nodiscard]] EnvironmentRecord MakeEnvironmentRecord(results::EnvironmentRecord common,
    const Foundation& foundation, BackendDiagnostics diagnostics);
[[nodiscard]] InitializationRecord MakeSetupCompleteInitialization(
    const Foundation& foundation, std::string observation);
[[nodiscard]] HostClockRecord MakeHostClockRecord(const Foundation& foundation,
    std::uint64_t sequenceIndex, std::optional<std::uint64_t> delta);
[[nodiscard]] WarmupRecord MakeWarmupRecord(const Foundation& foundation, std::uint64_t index);
[[nodiscard]] SampleRecord MakeSampleRecord(const Foundation& foundation, std::uint64_t index);

void ValidateEnvironmentRecord(const Foundation&, const EnvironmentRecord&);
void ValidateInitializationRecords(const Foundation&, std::span<const InitializationRecord>);
void ValidateHostClockRecords(const Foundation&, std::span<const HostClockRecord>);
void ValidateWarmupRecords(const Foundation&, std::span<const WarmupRecord>);
void ValidateSampleRecords(const Foundation&, std::span<const SampleRecord>);
// Row serializers also validate identity, status/progress and timing arithmetic.
void ValidateOperationRecord(const Foundation&, const OperationRecord&);
[[nodiscard]] SummaryRecord SummarizeSamples(const Foundation&, std::span<const SampleRecord>,
    Status processStatus, std::optional<FailurePhase> = std::nullopt,
    std::optional<std::string> = std::nullopt);
void ValidateSummary(const Foundation&, const SummaryRecord&, std::span<const SampleRecord>);
void ValidateBundle(const Foundation&, const EnvironmentRecord&,
    std::span<const InitializationRecord>, std::span<const HostClockRecord>,
    std::span<const WarmupRecord>, std::span<const SampleRecord>, const SummaryRecord&);

[[nodiscard]] std::string InitializationCsvHeader();
[[nodiscard]] std::string SamplesCsvHeader();
[[nodiscard]] std::string WarmupCsvHeader();
[[nodiscard]] std::string HostClockHeader();
[[nodiscard]] std::string SerializeEnvironmentJson(const Foundation&, const EnvironmentRecord&);
[[nodiscard]] std::string SerializeInitializationCsv(const Foundation&, std::span<const InitializationRecord>);
[[nodiscard]] std::string SerializeHostClockRow(const Foundation&, const HostClockRecord&);
[[nodiscard]] std::string SerializeHostClockCsv(const Foundation&, std::span<const HostClockRecord>);
[[nodiscard]] std::string SerializeWarmupRow(const Foundation&, const WarmupRecord&);
[[nodiscard]] std::string SerializeWarmupCsv(const Foundation&, std::span<const WarmupRecord>);
[[nodiscard]] std::string SerializeSampleRow(const Foundation&, const SampleRecord&);
[[nodiscard]] std::string SerializeSamplesCsv(const Foundation&, std::span<const SampleRecord>);
[[nodiscard]] std::string SerializeSummaryJson(const Foundation&, const SummaryRecord&,
    std::span<const SampleRecord>);

} // namespace computelab::ex2::stage5::evidence

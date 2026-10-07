#pragma once

#include "ex2/Ex2Stage6Plan.hpp"
#include "ex2/Ex2CorrectnessFoundation.hpp"

namespace computelab::ex2::stage6::evidence
{
inline constexpr std::uint32_t SchemaVersion = 2;
inline constexpr std::string_view ProtocolVersion = "1.4", EvidenceKind = "diagnostic", ExperimentId = "EX-2";
using Status = computelab::ex2::evidence::OperationStatus;
using FailurePhase = computelab::ex2::evidence::FailurePhase;
using CorrectnessProgress = computelab::ex2::evidence::CorrectnessProgress;
using BackendDiagnostics = computelab::ex2::evidence::BackendDiagnostics;
using InitializationRecord = computelab::ex2::evidence::InitializationRecord;
using InputIdentity = computelab::ex2::evidence::I7CorrectnessInputIdentity;

struct FoundationRequest
{
    PlannedSlot slot;
    std::string runId, machineId;
    SuppliedGpuIdentity gpuIdentity;
    std::string sourceRevision, executableSha256;
    std::optional<std::string> shaderSha256;
};
struct Plan
{
    PlannedSlot slot;
    std::string runId;
    SeriesIdentityContext seriesIdentity;
    std::string comparisonConditionId, seriesId;
};
class Foundation final
{
public:
    [[nodiscard]] const Plan& Identity() const noexcept { return plan_; }
    [[nodiscard]] const std::string& InputSha256() const noexcept { return inputSha256_; }
    [[nodiscard]] const std::string& ExpectedOutputSha256() const noexcept { return expectedOutputSha256_; }
private:
    Foundation(Plan, std::string, std::string);
    Plan plan_;
    std::string inputSha256_, expectedOutputSha256_;
    friend Foundation MakeWordFoundation(const FoundationRequest&, const InputIdentity&, std::span<const std::uint32_t>);
    friend Foundation MakeByteFoundation(const FoundationRequest&, const InputIdentity&, std::span<const std::uint8_t>);
};
[[nodiscard]] Foundation MakeWordFoundation(const FoundationRequest&, const InputIdentity&, std::span<const std::uint32_t>);
[[nodiscard]] Foundation MakeByteFoundation(const FoundationRequest&, const InputIdentity&, std::span<const std::uint8_t>);
void ValidatePlan(const Plan&);

struct EnvironmentRecord
{
    results::EnvironmentRecord common;
    Plan plan;
    std::string inputSha256, expectedOutputSha256;
    BackendDiagnostics backendDiagnostics;
    std::string evidenceKind{EvidenceKind};
};
struct SampleRecord
{
    Plan plan;
    std::uint64_t sampleIndex{};
    CorrectnessProgress correctness;
    Status status{Status::Incomplete};
    std::optional<FailurePhase> failurePhase;
    std::optional<std::string> errorCode;
    std::optional<std::uint64_t> hostSubmissionNanoseconds, hostWaitNanoseconds, hostCompletionNanoseconds;
    std::optional<std::uint64_t> nativeDeviceIntervalNanoseconds;
};
struct SummaryRecord
{
    Plan plan;
    Status processStatus{Status::Incomplete};
    std::optional<FailurePhase> failurePhase;
    std::optional<std::string> errorCode;
    std::uint64_t recordedSampleCount{}, successfulSampleCount{}, validationFailures{}, failedSampleCount{};
    results::MetricSummary hostSubmissionNanoseconds, hostWaitNanoseconds, hostCompletionNanoseconds;
    std::optional<results::MetricSummary> nativeDeviceIntervalNanoseconds;
};
[[nodiscard]] EnvironmentRecord MakeEnvironmentRecord(results::EnvironmentRecord, const Foundation&, BackendDiagnostics);
[[nodiscard]] InitializationRecord MakeSetupCompleteInitialization(const Foundation&, std::string);
[[nodiscard]] SampleRecord MakeSampleRecord(const Foundation&, std::uint64_t);
void ValidateEnvironmentRecord(const Foundation&, const EnvironmentRecord&);
void ValidateInitializationRecords(const Plan&, std::span<const InitializationRecord>);
void ValidateSampleRecord(const SampleRecord&);
void ValidateSamples(const Plan&, std::span<const SampleRecord>);
[[nodiscard]] SummaryRecord SummarizeSamples(const Plan&, std::span<const SampleRecord>, Status,
    std::optional<FailurePhase> = std::nullopt, std::optional<std::string> = std::nullopt);
void ValidateSummary(const SummaryRecord&, std::span<const SampleRecord>);
void ValidateBundle(const Foundation&, const EnvironmentRecord&, std::span<const InitializationRecord>,
    std::span<const SampleRecord>, const SummaryRecord&);
// One results-format algorithm shared with positional-window analysis. Input
// order is retained sample_index order; this helper never filters observations.
[[nodiscard]] results::MetricSummary SummarizeMetric(std::span<const std::uint64_t>);
[[nodiscard]] std::string SerializeMetricJson(const results::MetricSummary&);
[[nodiscard]] std::string InitializationCsvHeader();
[[nodiscard]] std::string SamplesCsvHeader();
[[nodiscard]] std::string SerializeEnvironmentJson(const Foundation&, const EnvironmentRecord&);
[[nodiscard]] std::string SerializeInitializationCsv(const Plan&, std::span<const InitializationRecord>);
[[nodiscard]] std::string SerializeSampleRow(const SampleRecord&);
[[nodiscard]] std::string SerializeSamplesCsv(const Plan&, std::span<const SampleRecord>);
[[nodiscard]] std::string SerializeSummaryJson(const SummaryRecord&, std::span<const SampleRecord>);

// Shared deterministic formatting primitives; no filesystem or control surface.
namespace detail
{
bool IsSha256(std::string_view);
std::string Json(std::string_view);
std::string Json(const std::string&);
std::string Json(const char*);
std::string Json(std::uint64_t);
std::string Json(double);
std::string Json(bool);
template <typename T> std::string Json(const std::optional<T>& value) { return value ? Json(*value) : "null"; }
template <typename T> void Field(std::string& out, std::string_view key, const T& value)
{ out += ',' + Json(key) + ':' + Json(value); }
std::string WorkloadJson(const WorkloadConfiguration&);
} // namespace detail
} // namespace computelab::ex2::stage6::evidence

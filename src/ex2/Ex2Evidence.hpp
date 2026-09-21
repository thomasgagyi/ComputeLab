#pragma once

#include "ex2/Ex2Configuration.hpp"
#include "results/ResultRecords.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace computelab::ex2::evidence
{

inline constexpr std::uint32_t SchemaVersion = 2U;
inline constexpr std::string_view ExperimentId = "EX-2";
inline constexpr std::string_view EvidenceKind = "correctness";

enum class OperationStatus
{
    Ok,
    ValidationFailed,
    SubmitFailed,
    WaitFailed,
    Timeout,
    DeviceLost,
    TimestampInvalid,
    Incomplete,
};

// This is an implementation-level diagnostic vocabulary for the schema's
// existing failure_phase string. It does not add schema fields or define a
// project-wide native error taxonomy.
enum class FailurePhase
{
    Configuration,
    InputGeneration,
    BackendInitialization,
    ResourceAllocation,
    Submission,
    CompletionWait,
    Readback,
    Validation,
    Timing,
    EvidenceSerialization,
    EvidencePublication,
    Interrupted,
};

namespace error_code
{
inline constexpr std::string_view InvalidConfiguration =
    "invalid_configuration";
inline constexpr std::string_view InputGenerationFailed =
    "input_generation_failed";
inline constexpr std::string_view BackendInitializationFailed =
    "backend_initialization_failed";
inline constexpr std::string_view ResourceAllocationFailed =
    "resource_allocation_failed";
inline constexpr std::string_view SubmissionFailed = "submission_failed";
inline constexpr std::string_view CompletionFailed = "completion_failed";
inline constexpr std::string_view ReadbackFailed = "readback_failed";
inline constexpr std::string_view OutputMismatch = "output_mismatch";
inline constexpr std::string_view OperationTimeout = "operation_timeout";
inline constexpr std::string_view DeviceLost = "device_lost";
inline constexpr std::string_view TimestampInvalid = "timestamp_invalid";
inline constexpr std::string_view EvidenceSerializationFailed =
    "evidence_serialization_failed";
inline constexpr std::string_view EvidencePublicationFailed =
    "evidence_publication_failed";
inline constexpr std::string_view Interrupted = "interrupted";
} // namespace error_code

struct CorrectnessProgress
{
    bool expectedOutputGenerated{};
    bool operationCompleted{};
    bool outputObserved{};
    bool comparisonPerformed{};
    std::optional<bool> validationPassed;
};

struct CorrectnessPlan
{
    std::string runId;
    SeriesIdentityContext seriesIdentity;
    std::string comparisonConditionId;
    std::string seriesId;
};

struct BackendDiagnostics
{
    std::string implementation;
    std::optional<std::string> streamFlags;
    std::optional<std::uint64_t> queueFamilyIndex;
    std::optional<std::uint64_t> queueFlags;
    std::optional<std::uint64_t> queueCount;
    std::optional<std::uint64_t> timestampValidBits;
    std::optional<double> timestampPeriodNanoseconds;
    std::optional<std::uint64_t> inputMemoryFlags;
    std::optional<std::uint64_t> outputMemoryFlags;
    std::optional<std::uint64_t> uploadMemoryFlags;
    std::optional<std::uint64_t> readbackMemoryFlags;
    bool nativeMarkersEnabled{};
    std::optional<std::string> nativeTimingMethod;
    std::optional<std::uint64_t> nativeTimingResolutionNanoseconds;
    std::optional<std::uint64_t> nativeTimingStartStage;
    std::optional<std::uint64_t> nativeTimingStopStage;
    std::optional<std::uint64_t> nativeDurationEnvelopeNanoseconds;
};

struct EnvironmentRecord
{
    results::EnvironmentRecord common;
    CorrectnessPlan plan;
    std::string inputSha256;
    std::string expectedOutputSha256;
    BackendDiagnostics backendDiagnostics;
};

struct InitializationRecord
{
    std::string runId;
    Backend backend{};
    std::uint64_t processIndex{};
    std::uint64_t sequenceIndex{};
    std::string category;
    std::optional<std::string> workload;
    std::optional<std::string> variant;
    std::optional<std::uint64_t> elementCount;
    std::string metric;
    std::optional<std::uint64_t> durationNanoseconds;
    std::optional<std::string> observation;
};

struct SampleRecord
{
    CorrectnessPlan plan;
    std::uint64_t sampleIndex{};
    CorrectnessProgress correctness;
    OperationStatus status{OperationStatus::Incomplete};
    std::optional<FailurePhase> failurePhase;
    std::optional<std::string> errorCode;
    std::optional<std::uint64_t> hostSubmissionNanoseconds;
    std::optional<std::uint64_t> hostWaitNanoseconds;
    std::optional<std::uint64_t> hostCompletionNanoseconds;
    std::optional<std::uint64_t> nativeDeviceIntervalNanoseconds;
};

struct SummaryRecord
{
    CorrectnessPlan plan;
    OperationStatus processStatus{OperationStatus::Incomplete};
    std::optional<FailurePhase> failurePhase;
    std::optional<std::string> errorCode;
    std::uint64_t recordedSampleCount{};
    std::uint64_t validationFailures{};
    std::uint64_t failedSampleCount{};
};

[[nodiscard]] std::string_view ToString(OperationStatus value) noexcept;
[[nodiscard]] std::string_view ToString(FailurePhase value) noexcept;

[[nodiscard]] CorrectnessPlan MakeCorrectnessPlan(
    std::string runId,
    SeriesIdentityContext seriesIdentity);
[[nodiscard]] SampleRecord MakeSampleRecord(
    const CorrectnessPlan& plan,
    std::uint64_t sampleIndex);

void ValidateCorrectnessPlan(const CorrectnessPlan& plan);
void ValidateEnvironmentRecord(const EnvironmentRecord& record);
void ValidateInitializationRecords(
    const CorrectnessPlan& plan,
    std::span<const InitializationRecord> records);
void ValidateSampleRecord(const SampleRecord& record);
void ValidateSamples(
    const CorrectnessPlan& plan,
    std::span<const SampleRecord> samples);
void ValidateSummary(
    const SummaryRecord& summary,
    std::span<const SampleRecord> samples);
void ValidateEvidenceBundle(
    const EnvironmentRecord& environment,
    std::span<const InitializationRecord> initialization,
    std::span<const SampleRecord> samples,
    const SummaryRecord& summary);

[[nodiscard]] std::string InitializationCsvHeader();
[[nodiscard]] std::string SamplesCsvHeader();
[[nodiscard]] std::string SerializeEnvironmentJson(
    const EnvironmentRecord& record);
[[nodiscard]] std::string SerializeInitializationCsv(
    const CorrectnessPlan& plan,
    std::span<const InitializationRecord> records);
[[nodiscard]] std::string SerializeSamplesCsv(
    const CorrectnessPlan& plan,
    std::span<const SampleRecord> records);
[[nodiscard]] SummaryRecord SummarizeSamples(
    const CorrectnessPlan& plan,
    std::span<const SampleRecord> samples,
    OperationStatus processStatus,
    std::optional<FailurePhase> failurePhase = std::nullopt,
    std::optional<std::string> errorCode = std::nullopt);
// Raw samples are required at serialization time so caller-supplied summary
// totals cannot be serialized without independent reconstruction.
[[nodiscard]] std::string SerializeSummaryJson(
    const SummaryRecord& record,
    std::span<const SampleRecord> samples);

} // namespace computelab::ex2::evidence

#include "ex2/Ex2Evidence.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <stdexcept>
#include <type_traits>

namespace computelab::ex2::evidence
{
namespace
{

constexpr std::string_view kInitializationHeader =
    "schema_version,run_id,experiment_id,backend,process_index,sequence_index,category,workload,variant,element_count,metric,duration_ns,observation";
constexpr std::string_view kSamplesHeader =
    "schema_version,run_id,experiment_id,comparison_condition_id,series_id,backend,workload,variant,seed,element_count,byte_count,index_pattern,counter_count,iteration_count,transfer_direction,execution_mode,instrument_mode,warmup_count,planned_sample_count,block_index,order_slot,process_index,sample_index,validation_passed,status,failure_phase,error_code,host_submission_ns,host_wait_ns,host_completion_ns,native_device_interval_ns";

struct WorkloadFields
{
    std::string_view workload;
    std::string_view variant;
    std::optional<std::uint64_t> elementCount;
    std::optional<std::uint64_t> byteCount;
    std::optional<std::string_view> indexPattern;
    std::optional<std::uint64_t> counterCount;
    std::optional<std::uint64_t> iterationCount;
    std::optional<std::string_view> transferDirection;
};

template <typename T>
void AppendInteger(std::string& output, T value)
{
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{})
        throw std::runtime_error("integer serialization failed");
    output.append(buffer.data(), end);
}

void AppendJsonString(std::string& output, std::string_view value)
{
    constexpr char hex[] = "0123456789abcdef";
    output.push_back('"');
    for (const unsigned char character : value)
    {
        switch (character)
        {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (character < 0x20U)
            {
                output += "\\u00";
                output.push_back(hex[character >> 4U]);
                output.push_back(hex[character & 0x0FU]);
            }
            else
            {
                output.push_back(static_cast<char>(character));
            }
            break;
        }
    }
    output.push_back('"');
}

void AppendJsonBoolean(std::string& output, bool value)
{
    output += value ? "true" : "false";
}

void AppendJsonDouble(std::string& output, double value)
{
    if (!std::isfinite(value))
        throw std::invalid_argument("JSON binary64 values must be finite");
    std::array<char, 32> buffer{};
    const auto [end, error] = std::to_chars(
        buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{})
        throw std::runtime_error("binary64 serialization failed");
    output.append(buffer.data(), end);
}

template <typename T, typename Append>
void AppendJsonOptional(
    std::string& output,
    const std::optional<T>& value,
    Append append)
{
    if (value.has_value()) append(*value);
    else output += "null";
}

void AppendCsvField(std::string& output, std::string_view value)
{
    if (value.find_first_of(",\"\r\n") == std::string_view::npos)
    {
        output.append(value);
        return;
    }
    output.push_back('"');
    for (const char character : value)
    {
        if (character == '"') output.push_back('"');
        output.push_back(character);
    }
    output.push_back('"');
}

template <typename T, typename Append>
void AppendCsvOptional(
    std::string& output,
    const std::optional<T>& value,
    Append append)
{
    if (value.has_value()) append(*value);
}

bool IsLowerHex(std::string_view value, std::size_t expectedSize) noexcept
{
    return value.size() == expectedSize
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

bool IsDiagnosticIdentifier(std::string_view value) noexcept
{
    return !value.empty() && value.size() <= 128U
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= 'a' && character <= 'z')
                || (character >= 'A' && character <= 'Z')
                || (character >= '0' && character <= '9')
                || character == '.' || character == '_' || character == '-';
        });
}

WorkloadFields Fields(const WorkloadConfiguration& configuration)
{
    return std::visit([](const auto& value) -> WorkloadFields {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, LinearConfiguration>)
        {
            return {"A", computelab::ex2::ToString(value.variant), value.elementCount};
        }
        else if constexpr (std::is_same_v<T, IndexedConfiguration>)
        {
            return {"B", computelab::ex2::ToString(value.variant), value.elementCount,
                std::nullopt, computelab::ex2::ToString(value.indexPattern)};
        }
        else if constexpr (std::is_same_v<T, ContentionConfiguration>)
        {
            return {"C", "C", value.elementCount, std::nullopt,
                std::nullopt, value.activeCounterCount};
        }
        else if constexpr (std::is_same_v<T, IterativeConfiguration>)
        {
            return {"D", computelab::ex2::ToString(value.variant), value.elementCount,
                std::nullopt, std::nullopt, std::nullopt,
                value.iterationCount};
        }
        else
        {
            return {"E", computelab::ex2::ToString(value.variant), std::nullopt,
                value.byteCount, std::nullopt, std::nullopt, std::nullopt,
                computelab::ex2::ToString(value.direction)};
        }
    }, configuration.parameters);
}

bool SamePlan(const CorrectnessPlan& left, const CorrectnessPlan& right)
{
    return left.runId == right.runId
        && left.comparisonConditionId == right.comparisonConditionId
        && left.seriesId == right.seriesId
        && SeriesCanonicalJson(left.seriesIdentity)
            == SeriesCanonicalJson(right.seriesIdentity);
}

bool HasAnyTiming(const SampleRecord& record) noexcept
{
    return record.hostSubmissionNanoseconds.has_value()
        || record.hostWaitNanoseconds.has_value()
        || record.hostCompletionNanoseconds.has_value()
        || record.nativeDeviceIntervalNanoseconds.has_value();
}

void RequireFailureContext(
    OperationStatus status,
    const std::optional<FailurePhase>& phase,
    const std::optional<std::string>& code)
{
    if (status == OperationStatus::Ok)
    {
        if (phase.has_value() || code.has_value())
            throw std::invalid_argument(
                "ok correctness outcome cannot contain failure context");
        return;
    }
    if (!phase.has_value() || !code.has_value()
        || !IsDiagnosticIdentifier(*code))
    {
        throw std::invalid_argument(
            "failed or incomplete correctness outcome requires a bounded failure phase and error code");
    }
}

bool IsDefinedFailurePhase(FailurePhase phase) noexcept
{
    switch (phase)
    {
    case FailurePhase::Configuration:
    case FailurePhase::InputGeneration:
    case FailurePhase::BackendInitialization:
    case FailurePhase::ResourceAllocation:
    case FailurePhase::Submission:
    case FailurePhase::CompletionWait:
    case FailurePhase::Readback:
    case FailurePhase::Validation:
    case FailurePhase::Timing:
    case FailurePhase::EvidenceSerialization:
    case FailurePhase::EvidencePublication:
    case FailurePhase::Interrupted:
        return true;
    }
    return false;
}

void ValidateStatusPhase(OperationStatus status, FailurePhase phase)
{
    if (!IsDefinedFailurePhase(phase))
        throw std::invalid_argument("correctness failure phase is invalid");

    const bool valid = [&] {
        switch (status)
        {
        case OperationStatus::Ok:
            return false;
        case OperationStatus::ValidationFailed:
            return phase == FailurePhase::Validation;
        case OperationStatus::SubmitFailed:
            return phase == FailurePhase::Submission;
        case OperationStatus::WaitFailed:
            return phase == FailurePhase::CompletionWait;
        case OperationStatus::Timeout:
            return phase == FailurePhase::Submission
                || phase == FailurePhase::CompletionWait;
        case OperationStatus::DeviceLost:
            return phase == FailurePhase::BackendInitialization
                || phase == FailurePhase::Submission
                || phase == FailurePhase::CompletionWait
                || phase == FailurePhase::Readback;
        case OperationStatus::TimestampInvalid:
            return false;
        case OperationStatus::Incomplete:
            return true;
        }
        return false;
    }();
    if (!valid)
        throw std::invalid_argument(
            "correctness status and failure phase are inconsistent");
}

void ValidateCorrectnessProgress(const SampleRecord& record)
{
    const auto& value = record.correctness;
    if (value.operationCompleted && !value.expectedOutputGenerated)
        throw std::invalid_argument(
            "completed operation requires an expected output");
    if (value.outputObserved && !value.operationCompleted)
        throw std::invalid_argument(
            "observed output requires a completed operation");
    if (value.comparisonPerformed
        && (!value.outputObserved || !value.expectedOutputGenerated))
    {
        throw std::invalid_argument(
            "output comparison requires expected and observed output");
    }
    if (value.validationPassed.has_value() != value.comparisonPerformed)
        throw std::invalid_argument(
            "validation result exists if and only if comparison was performed");

    switch (record.status)
    {
    case OperationStatus::Ok:
        if (!value.expectedOutputGenerated || !value.operationCompleted
            || !value.outputObserved || !value.comparisonPerformed
            || value.validationPassed != true)
        {
            throw std::invalid_argument(
                "ok correctness row requires completed observed passing validation");
        }
        break;
    case OperationStatus::ValidationFailed:
        if (!value.expectedOutputGenerated || !value.operationCompleted
            || !value.outputObserved || !value.comparisonPerformed
            || value.validationPassed != false)
        {
            throw std::invalid_argument(
                "validation_failed row requires completed observed failing validation");
        }
        break;
    case OperationStatus::TimestampInvalid:
        throw std::invalid_argument(
            "timestamp_invalid is not applicable to correctness-only evidence");
    case OperationStatus::Incomplete:
        if (value.comparisonPerformed || value.validationPassed.has_value())
            throw std::invalid_argument(
                "incomplete operation cannot claim a completed comparison");
        break;
    default:
        if (value.operationCompleted || value.outputObserved
            || value.comparisonPerformed || value.validationPassed.has_value())
        {
            throw std::invalid_argument(
                "execution failure before validation cannot claim completed or observed output");
        }
        break;
    }
}

void AppendCommonEnvironment(
    std::string& output,
    const results::EnvironmentRecord& value)
{
    output += "\"schema_version\":"; AppendInteger(output, value.schemaVersion);
    output += ",\"experiment_id\":"; AppendJsonString(output, value.experimentId);
    output += ",\"run_id\":"; AppendJsonString(output, value.runId);
    output += ",\"timestamp_utc\":"; AppendJsonString(output, value.timestampUtc);
    output += ",\"git_commit\":"; AppendJsonString(output, value.gitCommit);
    output += ",\"git_dirty\":"; AppendJsonBoolean(output, value.gitDirty);
    output += ",\"machine_id\":"; AppendJsonString(output, value.machineId);
    output += ",\"os_name\":"; AppendJsonString(output, value.osName);
    output += ",\"os_version\":"; AppendJsonString(output, value.osVersion);
    output += ",\"cpu_name\":"; AppendJsonString(output, value.cpuName);
    output += ",\"system_memory_bytes\":"; AppendInteger(output, value.systemMemoryBytes);
    output += ",\"gpu_name\":"; AppendJsonOptional(output, value.gpuName,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"gpu_vendor\":"; AppendJsonOptional(output, value.gpuVendor,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"gpu_device_id\":"; AppendJsonOptional(output, value.gpuDeviceId,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"gpu_memory_bytes\":"; AppendJsonOptional(output, value.gpuMemoryBytes,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"nvidia_driver_version\":"; AppendJsonOptional(output, value.nvidiaDriverVersion,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"cuda_toolkit_version\":"; AppendJsonOptional(output, value.cudaToolkitVersion,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"cuda_runtime_version\":"; AppendJsonOptional(output, value.cudaRuntimeVersion,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"cuda_compute_capability\":"; AppendJsonOptional(output, value.cudaComputeCapability,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"vulkan_sdk_version\":"; AppendJsonOptional(output, value.vulkanSdkVersion,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"vulkan_device_api_version\":"; AppendJsonOptional(output, value.vulkanDeviceApiVersion,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"compiler_name\":"; AppendJsonString(output, value.compilerName);
    output += ",\"compiler_version\":"; AppendJsonString(output, value.compilerVersion);
    output += ",\"cmake_version\":"; AppendJsonString(output, value.cmakeVersion);
    output += ",\"ninja_version\":"; AppendJsonString(output, value.ninjaVersion);
    output += ",\"configure_preset\":"; AppendJsonString(output, value.configurePreset);
    output += ",\"build_type\":"; AppendJsonString(output, value.buildType);
    output += ",\"validation_enabled\":"; AppendJsonBoolean(output, value.validationEnabled);
    output += ",\"diagnostic_instrumentation\":";
    AppendJsonBoolean(output, value.diagnosticInstrumentation);
}

void AppendBackendDiagnostics(
    std::string& output,
    const BackendDiagnostics& value)
{
    output += "{\"implementation\":"; AppendJsonString(output, value.implementation);
    output += ",\"stream_flags\":"; AppendJsonOptional(output, value.streamFlags,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"queue_family_index\":"; AppendJsonOptional(output, value.queueFamilyIndex,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"queue_flags\":"; AppendJsonOptional(output, value.queueFlags,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"queue_count\":"; AppendJsonOptional(output, value.queueCount,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"timestamp_valid_bits\":"; AppendJsonOptional(output, value.timestampValidBits,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"timestamp_period_ns\":"; AppendJsonOptional(output, value.timestampPeriodNanoseconds,
        [&output](double item) { AppendJsonDouble(output, item); });
    output += ",\"input_memory_flags\":"; AppendJsonOptional(output, value.inputMemoryFlags,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"output_memory_flags\":"; AppendJsonOptional(output, value.outputMemoryFlags,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"upload_memory_flags\":"; AppendJsonOptional(output, value.uploadMemoryFlags,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"readback_memory_flags\":"; AppendJsonOptional(output, value.readbackMemoryFlags,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"native_markers_enabled\":";
    AppendJsonBoolean(output, value.nativeMarkersEnabled);
    output += ",\"native_timing_method\":"; AppendJsonOptional(output, value.nativeTimingMethod,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"native_timing_resolution_ns\":";
    AppendJsonOptional(output, value.nativeTimingResolutionNanoseconds,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"native_timing_start_stage\":";
    AppendJsonOptional(output, value.nativeTimingStartStage,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"native_timing_stop_stage\":";
    AppendJsonOptional(output, value.nativeTimingStopStage,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"native_duration_envelope_ns\":";
    AppendJsonOptional(output, value.nativeDurationEnvelopeNanoseconds,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output.push_back('}');
}

void AppendJsonWorkloadParameters(
    std::string& output,
    const WorkloadConfiguration& configuration)
{
    const auto fields = Fields(configuration);
    output += ",\"element_count\":"; AppendJsonOptional(output, fields.elementCount,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"byte_count\":"; AppendJsonOptional(output, fields.byteCount,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"index_pattern\":"; AppendJsonOptional(output, fields.indexPattern,
        [&output](std::string_view item) { AppendJsonString(output, item); });
    output += ",\"counter_count\":"; AppendJsonOptional(output, fields.counterCount,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"iteration_count\":"; AppendJsonOptional(output, fields.iterationCount,
        [&output](std::uint64_t item) { AppendInteger(output, item); });
    output += ",\"transfer_direction\":"; AppendJsonOptional(output, fields.transferDirection,
        [&output](std::string_view item) { AppendJsonString(output, item); });
}

void AppendMetricAdmission(std::string& output)
{
    output += "{\"gate0_admission\":\"not_applicable\","
        "\"evidence_scope\":\"correctness_only\","
        "\"summary_sample_inclusion\":\"inapplicable_no_timing_metrics\"}";
}

void ValidateBackendDiagnostics(const BackendDiagnostics& value)
{
    if (!IsDiagnosticIdentifier(value.implementation))
        throw std::invalid_argument(
            "backend implementation must be a bounded non-sensitive identifier");
    if (value.streamFlags.has_value()
        && !IsDiagnosticIdentifier(*value.streamFlags))
    {
        throw std::invalid_argument(
            "stream flags must be a bounded non-sensitive identifier");
    }
    if (value.timestampPeriodNanoseconds.has_value()
        && !std::isfinite(*value.timestampPeriodNanoseconds))
    {
        throw std::invalid_argument("timestamp period must be finite");
    }
    if (value.nativeMarkersEnabled || value.timestampValidBits.has_value()
        || value.timestampPeriodNanoseconds.has_value()
        || value.nativeTimingMethod.has_value()
        || value.nativeTimingResolutionNanoseconds.has_value()
        || value.nativeTimingStartStage.has_value()
        || value.nativeTimingStopStage.has_value()
        || value.nativeDurationEnvelopeNanoseconds.has_value())
    {
        throw std::invalid_argument(
            "correctness-only evidence cannot declare native timing instrumentation");
    }
}

SummaryRecord ReconstructSummary(
    const CorrectnessPlan& plan,
    std::span<const SampleRecord> samples,
    OperationStatus processStatus,
    std::optional<FailurePhase> failurePhase,
    std::optional<std::string> errorCode)
{
    ValidateSamples(plan, samples);
    RequireFailureContext(processStatus, failurePhase, errorCode);
    if (failurePhase.has_value())
        ValidateStatusPhase(processStatus, *failurePhase);

    const auto declaredCount = plan.seriesIdentity.plannedSampleCount;
    if (samples.size() > declaredCount)
        throw std::invalid_argument(
            "recorded correctness rows exceed the declared operation count");

    const auto failedCount = static_cast<std::uint64_t>(std::count_if(
        samples.begin(), samples.end(), [](const SampleRecord& sample) {
            return sample.status != OperationStatus::Ok;
        }));
    const auto validationFailures = static_cast<std::uint64_t>(std::count_if(
        samples.begin(), samples.end(), [](const SampleRecord& sample) {
            return sample.correctness.validationPassed == false;
        }));

    if (processStatus == OperationStatus::Ok
        && (samples.size() != declaredCount || failedCount != 0U))
    {
        throw std::invalid_argument(
            "successful correctness process requires every declared operation to pass");
    }
    if (processStatus == OperationStatus::ValidationFailed
        && validationFailures == 0U)
    {
        throw std::invalid_argument(
            "validation_failed process requires a failing validation row");
    }
    if (processStatus != OperationStatus::Ok
        && processStatus != OperationStatus::Incomplete
        && std::none_of(samples.begin(), samples.end(),
            [processStatus](const SampleRecord& sample) {
                return sample.status == processStatus;
            }))
    {
        throw std::invalid_argument(
            "specific failed process status requires a matching raw sample row");
    }

    return {
        plan, processStatus, std::move(failurePhase), std::move(errorCode),
        static_cast<std::uint64_t>(samples.size()), validationFailures,
        failedCount};
}

} // namespace

std::string_view ToString(OperationStatus value) noexcept
{
    switch (value)
    {
    case OperationStatus::Ok: return "ok";
    case OperationStatus::ValidationFailed: return "validation_failed";
    case OperationStatus::SubmitFailed: return "submit_failed";
    case OperationStatus::WaitFailed: return "wait_failed";
    case OperationStatus::Timeout: return "timeout";
    case OperationStatus::DeviceLost: return "device_lost";
    case OperationStatus::TimestampInvalid: return "timestamp_invalid";
    case OperationStatus::Incomplete: return "incomplete";
    }
    return "invalid";
}

std::string_view ToString(FailurePhase value) noexcept
{
    switch (value)
    {
    case FailurePhase::Configuration: return "configuration";
    case FailurePhase::InputGeneration: return "input_generation";
    case FailurePhase::BackendInitialization: return "backend_initialization";
    case FailurePhase::ResourceAllocation: return "resource_allocation";
    case FailurePhase::Submission: return "submission";
    case FailurePhase::CompletionWait: return "completion_wait";
    case FailurePhase::Readback: return "readback";
    case FailurePhase::Validation: return "validation";
    case FailurePhase::Timing: return "timing";
    case FailurePhase::EvidenceSerialization: return "evidence_serialization";
    case FailurePhase::EvidencePublication: return "evidence_publication";
    case FailurePhase::Interrupted: return "interrupted";
    }
    return "invalid";
}

CorrectnessPlan MakeCorrectnessPlan(
    std::string runId,
    SeriesIdentityContext seriesIdentity)
{
    CorrectnessPlan result;
    result.runId = std::move(runId);
    result.comparisonConditionId = ComparisonConditionId(seriesIdentity.condition);
    result.seriesId = SeriesId(seriesIdentity);
    result.seriesIdentity = std::move(seriesIdentity);
    ValidateCorrectnessPlan(result);
    return result;
}

SampleRecord MakeSampleRecord(
    const CorrectnessPlan& plan,
    std::uint64_t sampleIndex)
{
    ValidateCorrectnessPlan(plan);
    SampleRecord result;
    result.plan = plan;
    result.sampleIndex = sampleIndex;
    return result;
}

void ValidateCorrectnessPlan(const CorrectnessPlan& plan)
{
    if (!IsValidAnonymousIdentifier(plan.runId))
        throw std::invalid_argument(
            "correctness run_id must be an anonymous identifier");
    if (plan.seriesIdentity.warmupCount != 0U)
        throw std::invalid_argument(
            "correctness-only evidence cannot claim warm-up operations");
    if (!IsCorrectnessTestEligible(plan.seriesIdentity.condition.workload))
        throw std::invalid_argument(
            "workload is not eligible for the approved correctness-only portfolio");
    if (plan.seriesIdentity.condition.instrumentMode == InstrumentMode::N)
        throw std::invalid_argument(
            "correctness-only evidence cannot claim native timing instrumentation");
    if (plan.seriesIdentity.plannedSampleCount == 0U)
        throw std::invalid_argument(
            "correctness plan must declare at least one operation");
    const std::string expectedCondition =
        ComparisonConditionId(plan.seriesIdentity.condition);
    const std::string expectedSeries = SeriesId(plan.seriesIdentity);
    if (!IsLowerHex(plan.comparisonConditionId, 64U)
        || plan.comparisonConditionId != expectedCondition)
    {
        throw std::invalid_argument(
            "correctness comparison_condition_id is stale or invalid");
    }
    if (!IsLowerHex(plan.seriesId, 64U) || plan.seriesId != expectedSeries)
        throw std::invalid_argument(
            "correctness series_id is stale or invalid");
}

void ValidateEnvironmentRecord(const EnvironmentRecord& record)
{
    ValidateCorrectnessPlan(record.plan);
    const auto& common = record.common;
    const auto& series = record.plan.seriesIdentity;
    if (common.schemaVersion != SchemaVersion
        || common.experimentId != ExperimentId
        || common.runId != record.plan.runId
        || common.machineId != series.condition.machineId
        || common.gitCommit != series.sourceRevision)
    {
        throw std::invalid_argument(
            "environment common metadata disagrees with correctness identity");
    }
    if (!common.validationEnabled)
        throw std::invalid_argument(
            "correctness evidence requires validation_enabled=true");
    if (common.timestampUtc.empty() || common.osName.empty()
        || common.osVersion.empty() || common.cpuName.empty()
        || common.compilerName.empty() || common.compilerVersion.empty()
        || common.cmakeVersion.empty() || common.ninjaVersion.empty()
        || common.configurePreset.empty() || common.buildType.empty())
    {
        throw std::invalid_argument(
            "correctness environment is missing required provenance metadata");
    }
    if (!common.gpuName.has_value() || !common.gpuVendor.has_value()
        || !common.gpuDeviceId.has_value() || !common.gpuMemoryBytes.has_value()
        || !common.nvidiaDriverVersion.has_value())
    {
        throw std::invalid_argument(
            "backend correctness evidence requires physical GPU provenance");
    }
    if (series.backend == Backend::Cuda
        && (!common.cudaToolkitVersion.has_value()
            || !common.cudaRuntimeVersion.has_value()
            || !common.cudaComputeCapability.has_value()))
    {
        throw std::invalid_argument(
            "CUDA correctness evidence is missing CUDA provenance");
    }
    if (series.backend == Backend::Vulkan
        && (!common.vulkanSdkVersion.has_value()
            || !common.vulkanDeviceApiVersion.has_value()))
    {
        throw std::invalid_argument(
            "Vulkan correctness evidence is missing Vulkan provenance");
    }
    if (common.diagnosticInstrumentation
        != (series.condition.instrumentMode == InstrumentMode::P))
    {
        throw std::invalid_argument(
            "environment instrumentation declaration disagrees with identity");
    }
    if (!IsLowerHex(record.inputSha256, 64U)
        || !IsLowerHex(record.expectedOutputSha256, 64U))
    {
        throw std::invalid_argument(
            "correctness input and expected-output digests must be lowercase SHA-256");
    }
    ValidateBackendDiagnostics(record.backendDiagnostics);
}

void ValidateInitializationRecords(
    const CorrectnessPlan& plan,
    std::span<const InitializationRecord> records)
{
    ValidateCorrectnessPlan(plan);
    if (records.empty())
        throw std::invalid_argument(
            "correctness evidence requires initialization.csv evidence");
    const auto fields = Fields(plan.seriesIdentity.condition.workload);
    bool anyTimedInitialization = false;
    bool hasUntimedSetupComplete = false;
    for (std::size_t index = 0U; index < records.size(); ++index)
    {
        const auto& record = records[index];
        if (record.runId != plan.runId
            || record.backend != plan.seriesIdentity.backend
            || record.processIndex != plan.seriesIdentity.processIndex
            || record.sequenceIndex != index)
        {
            throw std::invalid_argument(
                "initialization identity or ordering disagrees with correctness plan");
        }
        if (!IsDiagnosticIdentifier(record.category)
            || !IsDiagnosticIdentifier(record.metric))
        {
            throw std::invalid_argument(
                "initialization category and metric must be bounded identifiers");
        }
        if (!record.durationNanoseconds.has_value()
            && (!record.observation.has_value() || record.observation->empty()))
        {
            throw std::invalid_argument(
                "initialization row needs a duration or truthful observation");
        }
        anyTimedInitialization = anyTimedInitialization
            || record.durationNanoseconds.has_value();
        hasUntimedSetupComplete = hasUntimedSetupComplete
            || (!record.durationNanoseconds.has_value()
                && record.metric == "setup_complete"
                && record.observation.has_value()
                && !record.observation->empty());
        if (record.observation.has_value() && record.observation->size() > 256U)
            throw std::invalid_argument(
                "initialization observation exceeds the bounded diagnostic limit");
        if (record.workload.has_value() && *record.workload != fields.workload)
            throw std::invalid_argument(
                "initialization workload disagrees with correctness plan");
        if (record.variant.has_value() && *record.variant != fields.variant)
            throw std::invalid_argument(
                "initialization variant disagrees with correctness plan");
        if (record.elementCount.has_value()
            && record.elementCount != fields.elementCount)
        {
            throw std::invalid_argument(
                "initialization element_count disagrees with correctness plan");
        }
    }
    if (!anyTimedInitialization && !hasUntimedSetupComplete)
        throw std::invalid_argument(
            "untimed initialization requires a truthful setup_complete observation");
}

void ValidateSampleRecord(const SampleRecord& record)
{
    ValidateCorrectnessPlan(record.plan);
    RequireFailureContext(record.status, record.failurePhase, record.errorCode);
    if (record.failurePhase.has_value())
        ValidateStatusPhase(record.status, *record.failurePhase);
    ValidateCorrectnessProgress(record);
    if (HasAnyTiming(record))
        throw std::invalid_argument(
            "correctness-only sample timing fields must be absent");
}

void ValidateSamples(
    const CorrectnessPlan& plan,
    std::span<const SampleRecord> samples)
{
    ValidateCorrectnessPlan(plan);
    for (std::size_t index = 0U; index < samples.size(); ++index)
    {
        ValidateSampleRecord(samples[index]);
        if (!SamePlan(samples[index].plan, plan)
            || samples[index].sampleIndex != index)
        {
            throw std::invalid_argument(
                "correctness sample identity or ordering disagrees with plan");
        }
    }
}

void ValidateSummary(
    const SummaryRecord& summary,
    std::span<const SampleRecord> samples)
{
    const SummaryRecord expected = ReconstructSummary(
        summary.plan, samples, summary.processStatus,
        summary.failurePhase, summary.errorCode);
    if (!SamePlan(summary.plan, expected.plan)
        || summary.recordedSampleCount != expected.recordedSampleCount
        || summary.validationFailures != expected.validationFailures
        || summary.failedSampleCount != expected.failedSampleCount)
    {
        throw std::invalid_argument(
            "correctness summary counters disagree with raw samples");
    }
}

void ValidateEvidenceBundle(
    const EnvironmentRecord& environment,
    std::span<const InitializationRecord> initialization,
    std::span<const SampleRecord> samples,
    const SummaryRecord& summary)
{
    ValidateEnvironmentRecord(environment);
    if (!SamePlan(environment.plan, summary.plan))
        throw std::invalid_argument(
            "environment and summary correctness identities disagree");
    ValidateInitializationRecords(environment.plan, initialization);
    ValidateSamples(environment.plan, samples);
    ValidateSummary(summary, samples);
}

std::string InitializationCsvHeader()
{
    return std::string(kInitializationHeader);
}

std::string SamplesCsvHeader()
{
    return std::string(kSamplesHeader);
}

std::string SerializeEnvironmentJson(const EnvironmentRecord& record)
{
    ValidateEnvironmentRecord(record);
    const auto& series = record.plan.seriesIdentity;
    const auto& condition = series.condition;
    const auto& workload = condition.workload;
    const auto fields = Fields(workload);

    std::string output{"{"};
    AppendCommonEnvironment(output, record.common);
    output += ",\"protocol_version\":"; AppendJsonString(output, condition.protocolVersion);
    output += ",\"evidence_kind\":\"correctness\",\"backend\":";
    AppendJsonString(output, computelab::ex2::ToString(series.backend));
    output += ",\"instrument_mode\":"; AppendJsonString(output, computelab::ex2::ToString(condition.instrumentMode));
    output += ",\"warmup_count\":"; AppendInteger(output, series.warmupCount);
    output += ",\"planned_sample_count\":"; AppendInteger(output, series.plannedSampleCount);
    output += ",\"comparison_condition_id\":"; AppendJsonString(output, record.plan.comparisonConditionId);
    output += ",\"series_id\":"; AppendJsonString(output, record.plan.seriesId);
    output += ",\"block_index\":"; AppendInteger(output, series.blockIndex);
    output += ",\"process_index\":"; AppendInteger(output, series.processIndex);
    output += ",\"order_slot\":"; AppendInteger(output, series.orderSlot);
    output += ",\"source_revision\":"; AppendJsonString(output, series.sourceRevision);
    output += ",\"executable_sha256\":"; AppendJsonString(output, series.executableSha256);
    output += ",\"shader_sha256\":"; AppendJsonOptional(output, series.shaderSha256,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"input_sha256\":"; AppendJsonString(output, record.inputSha256);
    output += ",\"expected_output_sha256\":"; AppendJsonString(output, record.expectedOutputSha256);
    output += ",\"gpu_uuid_identity\":"; AppendJsonString(output, condition.gpuIdentity.uuid);
    output += ",\"workload\":"; AppendJsonString(output, fields.workload);
    output += ",\"variant\":"; AppendJsonString(output, fields.variant);
    output += ",\"generator_revision\":"; AppendJsonString(output, workload.common.generatorRevision);
    output += ",\"seed\":"; AppendInteger(output, workload.common.seed);
    AppendJsonWorkloadParameters(output, workload);
    output += ",\"execution_mode\":"; AppendJsonString(output, computelab::ex2::ToString(workload.common.executionMode));
    output += ",\"operation_boundary\":"; AppendJsonString(output, computelab::ex2::ToString(workload.common.operationBoundary));
    output += ",\"backend_native\":"; AppendBackendDiagnostics(output, record.backendDiagnostics);
    output += "}\n";
    return output;
}

std::string SerializeInitializationCsv(
    const CorrectnessPlan& plan,
    std::span<const InitializationRecord> records)
{
    ValidateInitializationRecords(plan, records);
    std::string output{kInitializationHeader};
    output += "\r\n";
    for (const auto& record : records)
    {
        AppendInteger(output, SchemaVersion); output.push_back(',');
        AppendCsvField(output, record.runId); output.push_back(',');
        AppendCsvField(output, ExperimentId); output.push_back(',');
        AppendCsvField(output, computelab::ex2::ToString(record.backend)); output.push_back(',');
        AppendInteger(output, record.processIndex); output.push_back(',');
        AppendInteger(output, record.sequenceIndex); output.push_back(',');
        AppendCsvField(output, record.category); output.push_back(',');
        AppendCsvOptional(output, record.workload,
            [&output](const std::string& item) { AppendCsvField(output, item); }); output.push_back(',');
        AppendCsvOptional(output, record.variant,
            [&output](const std::string& item) { AppendCsvField(output, item); }); output.push_back(',');
        AppendCsvOptional(output, record.elementCount,
            [&output](std::uint64_t item) { AppendInteger(output, item); }); output.push_back(',');
        AppendCsvField(output, record.metric); output.push_back(',');
        AppendCsvOptional(output, record.durationNanoseconds,
            [&output](std::uint64_t item) { AppendInteger(output, item); }); output.push_back(',');
        AppendCsvOptional(output, record.observation,
            [&output](const std::string& item) { AppendCsvField(output, item); });
        output += "\r\n";
    }
    return output;
}

std::string SerializeSamplesCsv(
    const CorrectnessPlan& plan,
    std::span<const SampleRecord> records)
{
    ValidateSamples(plan, records);
    std::string output{kSamplesHeader};
    output += "\r\n";
    const auto& series = plan.seriesIdentity;
    const auto& condition = series.condition;
    const auto& workload = condition.workload;
    const auto fields = Fields(workload);
    for (const auto& record : records)
    {
        AppendInteger(output, SchemaVersion); output.push_back(',');
        AppendCsvField(output, plan.runId); output.push_back(',');
        AppendCsvField(output, ExperimentId); output.push_back(',');
        AppendCsvField(output, plan.comparisonConditionId); output.push_back(',');
        AppendCsvField(output, plan.seriesId); output.push_back(',');
        AppendCsvField(output, computelab::ex2::ToString(series.backend)); output.push_back(',');
        AppendCsvField(output, fields.workload); output.push_back(',');
        AppendCsvField(output, fields.variant); output.push_back(',');
        AppendInteger(output, workload.common.seed); output.push_back(',');
        AppendCsvOptional(output, fields.elementCount,
            [&output](std::uint64_t item) { AppendInteger(output, item); }); output.push_back(',');
        AppendCsvOptional(output, fields.byteCount,
            [&output](std::uint64_t item) { AppendInteger(output, item); }); output.push_back(',');
        AppendCsvOptional(output, fields.indexPattern,
            [&output](std::string_view item) { AppendCsvField(output, item); }); output.push_back(',');
        AppendCsvOptional(output, fields.counterCount,
            [&output](std::uint64_t item) { AppendInteger(output, item); }); output.push_back(',');
        AppendCsvOptional(output, fields.iterationCount,
            [&output](std::uint64_t item) { AppendInteger(output, item); }); output.push_back(',');
        AppendCsvOptional(output, fields.transferDirection,
            [&output](std::string_view item) { AppendCsvField(output, item); }); output.push_back(',');
        AppendCsvField(output, computelab::ex2::ToString(workload.common.executionMode)); output.push_back(',');
        AppendCsvField(output, computelab::ex2::ToString(condition.instrumentMode)); output.push_back(',');
        AppendInteger(output, series.warmupCount); output.push_back(',');
        AppendInteger(output, series.plannedSampleCount); output.push_back(',');
        AppendInteger(output, series.blockIndex); output.push_back(',');
        AppendInteger(output, series.orderSlot); output.push_back(',');
        AppendInteger(output, series.processIndex); output.push_back(',');
        AppendInteger(output, record.sampleIndex); output.push_back(',');
        AppendCsvOptional(output, record.correctness.validationPassed,
            [&output](bool item) { AppendJsonBoolean(output, item); }); output.push_back(',');
        AppendCsvField(output, ToString(record.status)); output.push_back(',');
        AppendCsvOptional(output, record.failurePhase,
            [&output](FailurePhase item) { AppendCsvField(output, ToString(item)); }); output.push_back(',');
        AppendCsvOptional(output, record.errorCode,
            [&output](const std::string& item) { AppendCsvField(output, item); }); output.push_back(',');
        AppendCsvOptional(output, record.hostSubmissionNanoseconds,
            [&output](std::uint64_t item) { AppendInteger(output, item); }); output.push_back(',');
        AppendCsvOptional(output, record.hostWaitNanoseconds,
            [&output](std::uint64_t item) { AppendInteger(output, item); }); output.push_back(',');
        AppendCsvOptional(output, record.hostCompletionNanoseconds,
            [&output](std::uint64_t item) { AppendInteger(output, item); }); output.push_back(',');
        AppendCsvOptional(output, record.nativeDeviceIntervalNanoseconds,
            [&output](std::uint64_t item) { AppendInteger(output, item); });
        output += "\r\n";
    }
    return output;
}

SummaryRecord SummarizeSamples(
    const CorrectnessPlan& plan,
    std::span<const SampleRecord> samples,
    OperationStatus processStatus,
    std::optional<FailurePhase> failurePhase,
    std::optional<std::string> errorCode)
{
    return ReconstructSummary(plan, samples, processStatus,
        std::move(failurePhase), std::move(errorCode));
}

std::string SerializeSummaryJson(
    const SummaryRecord& record,
    std::span<const SampleRecord> samples)
{
    ValidateSummary(record, samples);
    const auto& series = record.plan.seriesIdentity;
    const auto& condition = series.condition;
    const auto& workload = condition.workload;
    const auto fields = Fields(workload);

    std::string output{"{\"schema_version\":2,\"run_id\":"};
    AppendJsonString(output, record.plan.runId);
    output += ",\"experiment_id\":\"EX-2\",\"evidence_kind\":\"correctness\",\"process_status\":";
    AppendJsonString(output, ToString(record.processStatus));
    output += ",\"failure_phase\":"; AppendJsonOptional(output, record.failurePhase,
        [&output](FailurePhase item) { AppendJsonString(output, ToString(item)); });
    output += ",\"error_code\":"; AppendJsonOptional(output, record.errorCode,
        [&output](const std::string& item) { AppendJsonString(output, item); });
    output += ",\"sample_groups\":[{\"group\":{\"comparison_condition_id\":";
    AppendJsonString(output, record.plan.comparisonConditionId);
    output += ",\"series_id\":"; AppendJsonString(output, record.plan.seriesId);
    output += ",\"run_id\":"; AppendJsonString(output, record.plan.runId);
    output += ",\"backend\":"; AppendJsonString(output, computelab::ex2::ToString(series.backend));
    output += ",\"workload\":"; AppendJsonString(output, fields.workload);
    output += ",\"variant\":"; AppendJsonString(output, fields.variant);
    output += ",\"seed\":"; AppendInteger(output, workload.common.seed);
    AppendJsonWorkloadParameters(output, workload);
    output += ",\"execution_mode\":"; AppendJsonString(output, computelab::ex2::ToString(workload.common.executionMode));
    output += ",\"instrument_mode\":"; AppendJsonString(output, computelab::ex2::ToString(condition.instrumentMode));
    output += ",\"warmup_count\":"; AppendInteger(output, series.warmupCount);
    output += ",\"planned_sample_count\":"; AppendInteger(output, series.plannedSampleCount);
    output += ",\"block_index\":"; AppendInteger(output, series.blockIndex);
    output += ",\"order_slot\":"; AppendInteger(output, series.orderSlot);
    output += ",\"process_index\":"; AppendInteger(output, series.processIndex);
    output += "},\"recorded_sample_count\":"; AppendInteger(output, record.recordedSampleCount);
    output += ",\"validation_failures\":"; AppendInteger(output, record.validationFailures);
    output += ",\"failed_sample_count\":"; AppendInteger(output, record.failedSampleCount);
    output += ",\"metric_admission\":{\"host_submission_ns\":"; AppendMetricAdmission(output);
    output += ",\"host_wait_ns\":"; AppendMetricAdmission(output);
    output += ",\"host_completion_ns\":"; AppendMetricAdmission(output);
    output += ",\"native_device_interval_ns\":"; AppendMetricAdmission(output);
    output += "},\"metrics\":{\"host_submission_ns\":null,\"host_wait_ns\":null,"
        "\"host_completion_ns\":null,\"native_device_interval_ns\":null}}]}\n";
    return output;
}

} // namespace computelab::ex2::evidence

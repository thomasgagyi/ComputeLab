#include "ex2/Ex2Stage5Evidence.hpp"

#include "ex2/Ex2CpuOracles.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace computelab::ex2::stage5::evidence
{
namespace
{
namespace old = computelab::ex2::evidence;

void Require(bool condition, const char* message)
{
    if (!condition) throw std::invalid_argument(message);
}

bool Utf8(std::string_view value)
{
    for (std::size_t i = 0; i < value.size();)
    {
        const auto first = static_cast<unsigned char>(value[i++]);
        if (first < 0x80U) continue;
        unsigned count{};
        std::uint32_t code{}, minimum{};
        if (first >= 0xC2U && first <= 0xDFU) { count = 1; code = first & 0x1FU; minimum = 0x80U; }
        else if (first >= 0xE0U && first <= 0xEFU) { count = 2; code = first & 0x0FU; minimum = 0x800U; }
        else if (first >= 0xF0U && first <= 0xF4U) { count = 3; code = first & 0x07U; minimum = 0x10000U; }
        else return false;
        if (count > value.size() - i) return false;
        for (unsigned j = 0; j < count; ++j)
        {
            const auto next = static_cast<unsigned char>(value[i++]);
            if ((next & 0xC0U) != 0x80U) return false;
            code = (code << 6U) | (next & 0x3FU);
        }
        if (code < minimum || code > 0x10FFFFU || (code >= 0xD800U && code <= 0xDFFFU)) return false;
    }
    return true;
}

std::string Json(std::string_view value)
{
    Require(Utf8(value), "evidence text must be valid UTF-8");
    constexpr char hex[] = "0123456789abcdef";
    std::string output{"\""};
    for (const unsigned char c : value)
    {
        if (c == '"') output += "\\\"";
        else if (c == '\\') output += "\\\\";
        else if (c < 0x20U)
        {
            output += "\\u00"; output += hex[c >> 4U]; output += hex[c & 15U];
        }
        else output += static_cast<char>(c);
    }
    return output + '"';
}
std::string Json(const std::string& value) { return Json(std::string_view{value}); }
std::string Json(const char* value) { return Json(std::string_view{value}); }
std::string Json(bool value) { return value ? "true" : "false"; }
template <typename T> std::string Number(T value)
{
    if constexpr (std::is_floating_point_v<T>) Require(std::isfinite(value), "nonfinite evidence number");
    std::array<char, 64> buffer{};
    const auto [end, error] = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (error != std::errc{}) throw std::runtime_error("evidence number serialization failed");
    return {buffer.data(), end};
}
std::string Json(std::uint64_t value) { return Number(value); }
std::string Json(double value) { return Number(value); }
template <typename T> std::string Json(const std::optional<T>& value)
{
    return value ? Json(*value) : "null";
}
template <typename T> void Field(std::string& output, std::string_view key, const T& value)
{
    output += ',' + Json(key) + ':' + Json(value);
}
std::string Csv(std::initializer_list<std::string> fields)
{
    std::string output;
    bool first = true;
    for (const auto& value : fields)
    {
        Require(Utf8(value), "CSV text must be UTF-8");
        if (!first) output += ',';
        first = false;
        if (value.find_first_of(",\"\r\n") == std::string::npos) output += value;
        else
        {
            output += '"';
            for (const auto c : value) { if (c == '"') output += '"'; output += c; }
            output += '"';
        }
    }
    return output + "\r\n";
}
template <typename T> std::string CsvOptional(const std::optional<T>& value)
{
    if (!value) return {};
    if constexpr (std::is_same_v<T, std::string>) return *value;
    else if constexpr (std::is_same_v<T, bool>) return Json(*value);
    else return Number(*value);
}

bool SamplePhase(const Plan& plan) { return plan.condition.phase == Stage5Phase::D1Sample; }
std::string_view WorkloadLabel(const Plan& plan)
{
    return plan.condition.phase == Stage5Phase::A1Sentinel ? "A" : "D";
}
std::string_view VariantLabel(const Plan& plan)
{
    return plan.condition.phase == Stage5Phase::A1Sentinel ? "A1" : "D1";
}
Backend BackendFor(Stage5Backend value)
{
    Require(value == Stage5Backend::Cuda || value == Stage5Backend::Vulkan, "invalid Stage-5 backend");
    return value == Stage5Backend::Cuda ? Backend::Cuda : Backend::Vulkan;
}
void RequireProcess(const PlannedProcess& process)
{
    const auto plan = FrozenProcessPlan();
    Require(std::find(plan.begin(), plan.end(), process) != plan.end(), "invalid frozen process identity");
}
void MatchPlan(const Foundation& foundation, const Plan& plan)
{
    ValidatePlan(plan);
    const auto& expected = foundation.Identity();
    Require(plan.runId == expected.runId && plan.condition == expected.condition
        && plan.process == expected.process && plan.comparisonConditionId == expected.comparisonConditionId
        && plan.seriesId == expected.seriesId
        && SeriesCanonicalJson(plan.seriesIdentity) == SeriesCanonicalJson(expected.seriesIdentity),
        "record identity disagrees with Stage-5 foundation");
}
void MatchClock(const Foundation& foundation, const HostClockRecord& record)
{
    const auto& plan = foundation.Identity();
    Require(record.runId == plan.runId && record.seriesId == plan.seriesId
        && record.processIndex == plan.process.processIndex && record.sequenceIndex < ClockDeltaCount,
        "clock row identity or index disagrees with foundation");
}

void Outcome(Status status, const std::optional<FailurePhase>& phase, const std::optional<std::string>& code)
{
    if (status == Status::Ok)
    {
        Require(!phase && !code, "ok outcome cannot carry failure context"); return;
    }
    Require(phase && code && IsValidAnonymousIdentifier(*code), "failure requires phase and bounded error code");
    Require(old::ToString(*phase) != "invalid" && *phase != FailurePhase::Configuration
        && *phase != FailurePhase::InputGeneration, "pre-foundation or undefined failure phase");
    bool valid = false;
    switch (status)
    {
    case Status::ValidationFailed: valid = *phase == FailurePhase::Validation && *code == old::error_code::OutputMismatch; break;
    case Status::SubmitFailed: valid = *phase == FailurePhase::Submission && *code == old::error_code::SubmissionFailed; break;
    case Status::WaitFailed: valid = *phase == FailurePhase::CompletionWait && *code == old::error_code::CompletionFailed; break;
    case Status::Timeout:
        valid = (*phase == FailurePhase::Submission || *phase == FailurePhase::CompletionWait)
            && *code == old::error_code::OperationTimeout; break;
    case Status::DeviceLost:
        valid = (*phase == FailurePhase::BackendInitialization || *phase == FailurePhase::Submission
            || *phase == FailurePhase::CompletionWait || *phase == FailurePhase::Readback)
            && *code == old::error_code::DeviceLost; break;
    case Status::Incomplete:
        switch (*phase)
        {
        case FailurePhase::BackendInitialization: valid = *code == old::error_code::BackendInitializationFailed; break;
        case FailurePhase::ResourceAllocation: valid = *code == old::error_code::ResourceAllocationFailed; break;
        case FailurePhase::Submission: valid = *code == old::error_code::SubmissionFailed; break;
        case FailurePhase::CompletionWait: valid = *code == old::error_code::CompletionFailed; break;
        case FailurePhase::Readback: valid = *code == old::error_code::ReadbackFailed; break;
        case FailurePhase::Timing: valid = *code == old::error_code::TimestampInvalid; break;
        case FailurePhase::EvidenceSerialization: valid = *code == old::error_code::EvidenceSerializationFailed; break;
        case FailurePhase::EvidencePublication: valid = *code == old::error_code::EvidencePublicationFailed; break;
        case FailurePhase::Interrupted: valid = *code == old::error_code::Interrupted; break;
        default: break;
        }
        break;
    case Status::TimestampInvalid: break; // Native timestamp status is inapplicable in H.
    case Status::Ok: break;
    }
    Require(valid, "contradictory status, phase or error code");
}

void Text(std::string_view value)
{
    Require(!value.empty() && value.size() <= 1024U && Utf8(value)
        && value.find_first_of("\\/\r\n") == std::string_view::npos
        && value.find('\0') == std::string_view::npos, "missing, malformed or private-path provenance");
}
void Parameters(std::string& output, const Plan& plan)
{
    Field(output, "element_count", plan.condition.elementCount);
    output += ",\"byte_count\":null,\"index_pattern\":null,\"counter_count\":null";
    Field(output, "iteration_count", plan.condition.iterationCount);
    output += ",\"transfer_direction\":null";
}
std::string DiagnosticsJson(const BackendDiagnostics& d)
{
    std::string output = "{\"implementation\":" + Json(d.implementation);
    Field(output, "stream_flags", d.streamFlags);
    Field(output, "queue_family_index", d.queueFamilyIndex);
    Field(output, "queue_flags", d.queueFlags);
    Field(output, "queue_count", d.queueCount);
    output += ",\"timestamp_valid_bits\":null,\"timestamp_period_ns\":null";
    Field(output, "input_memory_flags", d.inputMemoryFlags);
    Field(output, "output_memory_flags", d.outputMemoryFlags);
    Field(output, "upload_memory_flags", d.uploadMemoryFlags);
    Field(output, "readback_memory_flags", d.readbackMemoryFlags);
    output += ",\"native_markers_enabled\":false,\"native_timing_method\":null,"
        "\"native_timing_resolution_ns\":null,\"native_timing_start_stage\":null,"
        "\"native_timing_stop_stage\":null,\"native_duration_envelope_ns\":null}";
    return output;
}

std::string MetricJson(const std::optional<results::MetricSummary>& metric)
{
    if (!metric) return "null";
    std::string output = "{\"sample_count\":" + Json(metric->sampleCount);
    Field(output, "minimum", metric->minimum); Field(output, "median", metric->median);
    Field(output, "mean", metric->mean); Field(output, "standard_deviation", metric->standardDeviation);
    Field(output, "coefficient_of_variation", metric->coefficientOfVariation); Field(output, "p95", metric->p95);
    return output + '}';
}

template <typename Select> results::MetricSummary SummarizeMetric(std::span<const SampleRecord> samples, Select select)
{
    // Binary64 sequential accumulation in already validated retained order.
    std::vector<double> values;
    for (const auto& sample : samples)
        if (sample.status == Status::Ok && sample.correctness.validationPassed == true)
            values.push_back(static_cast<double>(*select(sample)));
    if (values.empty()) return {};
    double sum = 0;
    for (const auto value : values) sum += value;
    const double mean = sum / static_cast<double>(values.size());
    std::optional<double> sd, cv;
    if (values.size() >= 2U)
    {
        double squared = 0;
        for (const auto value : values) { const auto difference = value - mean; squared += difference * difference; }
        sd = std::sqrt(squared / static_cast<double>(values.size() - 1U));
        if (mean != 0) cv = *sd / mean;
    }
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2U;
    const double median = values.size() % 2U ? values[middle] : (values[middle - 1U] + values[middle]) / 2.0;
    return {static_cast<std::uint64_t>(values.size()), values.front(), median, mean, sd, cv,
        values[values.size() - values.size() / 20U - 1U]};
}

enum class MetricIdentity { HostSubmission, HostWait, HostCompletion, NativeDeviceInterval };

std::string Admission(const Plan& plan, MetricIdentity metric)
{
    // The validated plan supplies phase/workload and H instrument context;
    // metric identity determines admission independently of summary inclusion.
    if (metric == MetricIdentity::NativeDeviceInterval)
        return "{\"gate0_admission\":\"not_applicable\",\"evidence_scope\":"
        "\"not_recorded_in_instrument_mode_h\",\"summary_sample_inclusion\":\"inapplicable_instrument_mode_h\"}";
    const bool a1 = plan.condition.phase == Stage5Phase::A1Sentinel;
    const bool decision = !a1 && metric == MetricIdentity::HostCompletion;
    const auto scope = a1 ? "diagnostic_only_a1_sentinel"
        : decision ? "stage5_d1_h_decision_host_completion" : "diagnostic_only_host_component";
    return "{\"gate0_admission\":" + Json(decision ? "pending_gate0_review" : "not_admitted")
        + ",\"evidence_scope\":" + Json(scope)
        + ",\"summary_sample_inclusion\":" + Json(SamplePhase(plan)
            ? "completed_correctness_passing_ok_rows" : "no_steady_state_samples") + '}';
}
} // namespace

WorkloadConfiguration WorkloadFor(const Condition& condition)
{
    Require(ValidateCondition(condition), "invalid Stage-5 condition");
    if (condition.phase == Stage5Phase::A1Sentinel)
        return MakeConfiguration(LinearConfiguration{LinearVariant::A1, A1ElementCount});
    return MakeConfiguration(IterativeConfiguration{IterativeVariant::D1, D1ElementCount, D1IterationCount});
}

void ValidatePlan(const Plan& plan)
{
    const auto workload = WorkloadFor(plan.condition);
    RequireProcess(plan.process);
    Require(IsValidAnonymousIdentifier(plan.runId), "invalid anonymous run ID");
    const auto& series = plan.seriesIdentity;
    Require(series.condition.protocolVersion == ProtocolVersion && series.condition.instrumentMode == computelab::ex2::InstrumentMode::H
        && series.condition.workload == workload && series.backend == BackendFor(plan.process.backend)
        && series.processIndex == plan.process.processIndex && series.blockIndex == plan.process.blockIndex
        && series.orderSlot == plan.process.orderSlot && series.warmupCount == plan.condition.selectedW.value_or(0U)
        && series.plannedSampleCount == plan.condition.measuredSampleCount,
        "Stage-5 plan conflicts with derived phase identity");
    Require(series.condition.gpuIdentity.uuid != "00000000-0000-0000-0000-000000000000", "zero GPU identity");
    Require(plan.comparisonConditionId == ComparisonConditionId(series.condition)
        && plan.seriesId == SeriesId(series), "stale Stage-5 canonical identity");
}

Foundation::Foundation(Plan plan, std::string inputSha256, std::string expectedOutputSha256)
    : plan_(std::move(plan)), inputSha256_(std::move(inputSha256)), expectedOutputSha256_(std::move(expectedOutputSha256)) {}

Foundation MakeWordFoundation(const FoundationRequest& request, std::span<const std::uint32_t> input,
    std::span<const std::uint32_t> expected)
{
    const auto workload = WorkloadFor(request.condition);
    RequireProcess(request.process);
    Plan plan{request.condition, request.process, request.runId,
        {{std::string(ProtocolVersion), request.machineId, request.gpuIdentity, workload, computelab::ex2::InstrumentMode::H},
            BackendFor(request.process.backend), request.process.processIndex, request.process.blockIndex,
            request.process.orderSlot, request.condition.selectedW.value_or(0U), request.condition.measuredSampleCount,
            request.sourceRevision, request.executableSha256, request.shaderSha256}, {}, {}};
    plan.comparisonConditionId = ComparisonConditionId(plan.seriesIdentity.condition);
    plan.seriesId = SeriesId(plan.seriesIdentity);
    ValidatePlan(plan);
    Require(input.size() == request.condition.elementCount && expected.size() == input.size(), "wrong typed word count");
    for (std::size_t i = 0; i < input.size(); ++i)
        Require(input[i] == Value(CoreInputSeed, i), "input disagrees with frozen seed/generator");
    const auto oracle = request.condition.phase == Stage5Phase::A1Sentinel
        ? ReferenceA1(input) : ReferenceD1(input, D1IterationCount).finalState;
    Require(std::equal(expected.begin(), expected.end(), oracle.begin()), "expected output disagrees with CPU oracle");
    return {std::move(plan), WordInputSha256(input), WordInputSha256(expected)};
}

EnvironmentRecord MakeEnvironmentRecord(results::EnvironmentRecord common, const Foundation& foundation, BackendDiagnostics diagnostics)
{
    EnvironmentRecord result{std::move(common), foundation.Identity(), foundation.InputSha256(),
        foundation.ExpectedOutputSha256(), std::move(diagnostics)};
    ValidateEnvironmentRecord(foundation, result); return result;
}

void ValidateEnvironmentRecord(const Foundation& foundation, const EnvironmentRecord& record)
{
    MatchPlan(foundation, record.plan);
    const auto& c = record.common;
    const auto& s = record.plan.seriesIdentity;
    Require(c.schemaVersion == SchemaVersion && c.experimentId == ExperimentId && c.runId == record.plan.runId
        && c.machineId == s.condition.machineId && c.gitCommit == s.sourceRevision && !c.gitDirty
        && !c.validationEnabled && !c.diagnosticInstrumentation && record.evidenceKind == EvidenceKind,
        "qualification provenance or ordinary H instrumentation disagrees");
    for (const auto* value : {&c.timestampUtc, &c.osName, &c.osVersion, &c.cpuName, &c.compilerName,
        &c.compilerVersion, &c.cmakeVersion, &c.ninjaVersion, &c.configurePreset, &c.buildType}) Text(*value);
    Require(c.timestampUtc.size() == 20U && c.timestampUtc[10] == 'T' && c.timestampUtc.back() == 'Z', "UTC timestamp required");
    Require(c.systemMemoryBytes != 0 && c.gpuMemoryBytes && *c.gpuMemoryBytes != 0, "physical memory provenance missing");
    for (const auto* value : {&c.gpuName, &c.gpuVendor, &c.gpuDeviceId, &c.nvidiaDriverVersion})
    { Require(value->has_value(), "GPU provenance missing"); Text(**value); }
    for (const auto* value : {&c.cudaToolkitVersion, &c.cudaRuntimeVersion, &c.cudaComputeCapability,
        &c.vulkanSdkVersion, &c.vulkanDeviceApiVersion}) if (*value) Text(**value);
    Require(s.backend == Backend::Cuda ? c.cudaToolkitVersion && c.cudaRuntimeVersion && c.cudaComputeCapability
        : c.vulkanSdkVersion && c.vulkanDeviceApiVersion, "backend provenance missing");
    Require(record.inputSha256 == foundation.InputSha256() && record.expectedOutputSha256 == foundation.ExpectedOutputSha256(),
        "environment digests disagree with typed foundation");
    const auto& d = record.backendDiagnostics;
    Require(IsValidAnonymousIdentifier(d.implementation) && (!d.streamFlags || IsValidAnonymousIdentifier(*d.streamFlags)), "invalid backend diagnostic identifier");
    Require(!d.nativeMarkersEnabled && !d.timestampValidBits && !d.timestampPeriodNanoseconds && !d.nativeTimingMethod
        && !d.nativeTimingResolutionNanoseconds && !d.nativeTimingStartStage && !d.nativeTimingStopStage
        && !d.nativeDurationEnvelopeNanoseconds, "H diagnostics cannot carry native timing metadata");
    Require(s.backend == Backend::Cuda ? d.streamFlags && !d.queueFamilyIndex && !d.queueFlags && !d.queueCount
        : !d.streamFlags && d.queueFamilyIndex && d.queueFlags && d.queueCount && *d.queueCount != 0,
        "backend diagnostics disagree with native backend");
}

InitializationRecord MakeSetupCompleteInitialization(const Foundation& f, std::string observation)
{
    const auto& p = f.Identity();
    InitializationRecord record{p.runId, p.seriesIdentity.backend, p.process.processIndex, 0U, "backend_setup",
        std::string(WorkloadLabel(p)), std::string(VariantLabel(p)), p.condition.elementCount,
        "setup_complete", std::nullopt, std::move(observation)};
    ValidateInitializationRecords(f, {&record, 1U}); return record;
}

void ValidateInitializationRecords(const Foundation& f, std::span<const InitializationRecord> records)
{
    Require(!records.empty(), "initialization evidence missing");
    const auto& p = f.Identity();
    bool setup = false;
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        const auto& r = records[i];
        Require(r.runId == p.runId && r.backend == p.seriesIdentity.backend && r.processIndex == p.process.processIndex
            && r.sequenceIndex == i, "initialization identity or sequence drift");
        Require(IsValidAnonymousIdentifier(r.category) && IsValidAnonymousIdentifier(r.metric), "invalid initialization identifier");
        Require((!r.workload || *r.workload == WorkloadLabel(p)) && (!r.variant || *r.variant == VariantLabel(p))
            && (!r.elementCount || *r.elementCount == p.condition.elementCount), "initialization workload drift");
        Require(r.durationNanoseconds || (r.observation && !r.observation->empty()), "missing initialization observation");
        if (r.observation) Require(Utf8(*r.observation) && r.observation->find('\0') == std::string::npos, "invalid initialization UTF-8");
        if (r.metric == "setup_complete")
        {
            Require(r.category == "backend_setup" && !r.durationNanoseconds && r.observation && !r.observation->empty(),
                "setup_complete must be a truthful untimed observation");
            setup = true;
        }
    }
    Require(setup, "setup_complete observation missing");
}

HostClockRecord MakeHostClockRecord(const Foundation& f, std::uint64_t index, std::optional<std::uint64_t> delta)
{
    const auto& p = f.Identity();
    HostClockRecord result{p.runId, p.seriesId, p.process.processIndex, index, delta};
    MatchClock(f, result); return result;
}
void ValidateHostClockRecords(const Foundation& f, std::span<const HostClockRecord> records)
{
    Require(records.size() == ClockDeltaCount, "wrong host-clock row count");
    for (std::size_t i = 0; i < records.size(); ++i)
    { MatchClock(f, records[i]); Require(records[i].sequenceIndex == i, "clock sequence drift"); }
    // Null/zero/coarse deltas are preserved; S5-I2 owns calibration adequacy.
}

WarmupRecord MakeWarmupRecord(const Foundation& f, std::uint64_t index)
{
    Require(!SamplePhase(f.Identity()) && index < DiagnosticObservationCount, "invalid diagnostic phase/index");
    WarmupRecord result; result.plan = f.Identity(); result.sequenceIndex = index; return result;
}
SampleRecord MakeSampleRecord(const Foundation& f, std::uint64_t index)
{
    Require(SamplePhase(f.Identity()) && index < SampleObservationCount, "invalid sample phase/index");
    SampleRecord result; result.plan = f.Identity(); result.sequenceIndex = index; return result;
}

void ValidateOperationRecord(const Foundation& f, const OperationRecord& r)
{
    MatchPlan(f, r.plan);
    Require(r.sequenceIndex < (SamplePhase(r.plan) ? SampleObservationCount : DiagnosticObservationCount), "operation index drift");
    Outcome(r.status, r.failurePhase, r.errorCode);
    const auto& c = r.correctness;
    Require(c.expectedOutputGenerated && (!c.outputObserved || c.operationCompleted)
        && (!c.comparisonPerformed || c.outputObserved) && c.validationPassed.has_value() == c.comparisonPerformed,
        "contradictory correctness progress");
    const bool compared = r.status == Status::Ok || r.status == Status::ValidationFailed;
    Require(compared ? c.operationCompleted && c.outputObserved && c.comparisonPerformed
            && c.validationPassed == (r.status == Status::Ok)
        : !c.comparisonPerformed && !c.validationPassed, "status conflicts with per-operation validation");
    const bool beforeCompletion = r.failurePhase == FailurePhase::BackendInitialization || r.failurePhase == FailurePhase::ResourceAllocation
        || r.failurePhase == FailurePhase::Submission || r.failurePhase == FailurePhase::CompletionWait;
    Require(!beforeCompletion || (!c.operationCompleted && !c.outputObserved), "execution failure cannot claim completion");
    Require(r.failurePhase != FailurePhase::Readback || (c.operationCompleted && !c.outputObserved), "readback failure progress is contradictory");
    Require(!r.nativeDeviceIntervalNanoseconds, "native timing in H");
    Require(!r.hostWaitNanoseconds || r.hostSubmissionNanoseconds, "wait timing requires submission prefix");
    if (r.hostCompletionNanoseconds)
    {
        Require(r.hostSubmissionNanoseconds && r.hostWaitNanoseconds, "completion timing requires both components");
        Require(*r.hostSubmissionNanoseconds <= std::numeric_limits<std::uint64_t>::max() - *r.hostWaitNanoseconds
            && *r.hostCompletionNanoseconds == *r.hostSubmissionNanoseconds + *r.hostWaitNanoseconds, "invalid H timing arithmetic");
    }
    Require(!c.operationCompleted || r.hostCompletionNanoseconds, "completed operation needs complete H timing");
    if (r.failurePhase == FailurePhase::BackendInitialization || r.failurePhase == FailurePhase::ResourceAllocation)
        Require(!r.hostSubmissionNanoseconds && !r.hostWaitNanoseconds && !r.hostCompletionNanoseconds, "pre-operation failure cannot claim timing");
    if (r.failurePhase == FailurePhase::Submission)
        Require(!r.hostWaitNanoseconds && !r.hostCompletionNanoseconds, "submission failure cannot claim wait/completion timing");
    if (r.failurePhase == FailurePhase::CompletionWait)
        Require(!r.hostWaitNanoseconds && !r.hostCompletionNanoseconds, "failed completion wait has no successful t2 timing");
}

void ValidateWarmupRecords(const Foundation& f, std::span<const WarmupRecord> records)
{
    Require(records.size() == (SamplePhase(f.Identity()) ? 0U : DiagnosticObservationCount), "wrong diagnostic count");
    for (std::size_t i = 0; i < records.size(); ++i)
    { ValidateOperationRecord(f, records[i]); Require(records[i].sequenceIndex == i, "diagnostic sequence drift"); }
}
void ValidateSampleRecords(const Foundation& f, std::span<const SampleRecord> records)
{
    Require(records.size() == (SamplePhase(f.Identity()) ? SampleObservationCount : 0U), "wrong measured count");
    for (std::size_t i = 0; i < records.size(); ++i)
    { ValidateOperationRecord(f, records[i]); Require(records[i].sequenceIndex == i, "sample sequence drift"); }
}

SummaryRecord SummarizeSamples(const Foundation& f, std::span<const SampleRecord> samples,
    Status status, std::optional<FailurePhase> phase, std::optional<std::string> code)
{
    ValidateSampleRecords(f, samples); Outcome(status, phase, code);
    SummaryRecord result;
    result.plan = f.Identity(); result.processStatus = status; result.failurePhase = phase; result.errorCode = code;
    result.recordedSampleCount = samples.size();
    for (const auto& r : samples)
    {
        result.validationFailures += r.correctness.validationPassed == false ? 1U : 0U;
        result.failedSampleCount += r.status != Status::Ok ? 1U : 0U;
    }
    if (SamplePhase(result.plan))
    {
        Require(status != Status::Ok || result.failedSampleCount == 0U, "successful process cannot contain failed samples");
        if (status != Status::Ok && status != Status::Incomplete)
            Require(std::any_of(samples.begin(), samples.end(), [status](const auto& r) { return r.status == status; }), "process failure lacks matching sample");
        result.hostSubmissionNanoseconds = SummarizeMetric(samples, [](const auto& r) { return r.hostSubmissionNanoseconds; });
        result.hostWaitNanoseconds = SummarizeMetric(samples, [](const auto& r) { return r.hostWaitNanoseconds; });
        result.hostCompletionNanoseconds = SummarizeMetric(samples, [](const auto& r) { return r.hostCompletionNanoseconds; });
    }
    return result;
}

void ValidateSummary(const Foundation& f, const SummaryRecord& r, std::span<const SampleRecord> samples)
{
    MatchPlan(f, r.plan);
    const auto expected = SummarizeSamples(f, samples, r.processStatus, r.failurePhase, r.errorCode);
    Require(r.recordedSampleCount == expected.recordedSampleCount && r.validationFailures == expected.validationFailures
        && r.failedSampleCount == expected.failedSampleCount
        && MetricJson(r.hostSubmissionNanoseconds) == MetricJson(expected.hostSubmissionNanoseconds)
        && MetricJson(r.hostWaitNanoseconds) == MetricJson(expected.hostWaitNanoseconds)
        && MetricJson(r.hostCompletionNanoseconds) == MetricJson(expected.hostCompletionNanoseconds)
        && !r.nativeDeviceIntervalNanoseconds, "summary disagrees with raw samples");
}

void ValidateBundle(const Foundation& f, const EnvironmentRecord& env, std::span<const InitializationRecord> initialization,
    std::span<const HostClockRecord> clock, std::span<const WarmupRecord> warmup,
    std::span<const SampleRecord> samples, const SummaryRecord& summary)
{
    ValidateEnvironmentRecord(f, env); ValidateInitializationRecords(f, initialization);
    ValidateHostClockRecords(f, clock); ValidateWarmupRecords(f, warmup);
    ValidateSampleRecords(f, samples); ValidateSummary(f, summary, samples);
    if (!SamplePhase(f.Identity()))
    {
        if (summary.processStatus == Status::Ok)
            Require(std::all_of(warmup.begin(), warmup.end(), [](const auto& r) { return r.status == Status::Ok; }), "successful diagnostic process contains failure");
        else if (summary.processStatus != Status::Incomplete)
            Require(std::any_of(warmup.begin(), warmup.end(), [&](const auto& r) { return r.status == summary.processStatus; }), "diagnostic failure lacks matching row");
    }
}

std::string InitializationCsvHeader() { return old::InitializationCsvHeader(); }
std::string SamplesCsvHeader() { return old::SamplesCsvHeader(); }
std::string WarmupCsvHeader()
{
    return "schema_version,run_id,series_id,process_index,sequence_index,host_submission_ns,host_wait_ns,host_completion_ns,status";
}
std::string HostClockHeader() { return std::string(HostClockCsvHeader); }

std::string SerializeEnvironmentJson(const Foundation& f, const EnvironmentRecord& r)
{
    ValidateEnvironmentRecord(f, r);
    const auto& p = r.plan; const auto& s = p.seriesIdentity; const auto& w = s.condition.workload;
    auto output = results::SerializeEnvironmentJson(r.common);
    output.resize(output.size() - 2U); // Extend the existing common JSON object.
    Field(output, "protocol_version", ProtocolVersion); Field(output, "evidence_kind", EvidenceKind);
    Field(output, "backend", computelab::ex2::ToString(s.backend)); Field(output, "instrument_mode", "H");
    Field(output, "warmup_count", s.warmupCount); Field(output, "planned_sample_count", s.plannedSampleCount);
    Field(output, "comparison_condition_id", p.comparisonConditionId); Field(output, "series_id", p.seriesId);
    Field(output, "block_index", s.blockIndex); Field(output, "process_index", s.processIndex); Field(output, "order_slot", s.orderSlot);
    Field(output, "source_revision", s.sourceRevision); Field(output, "executable_sha256", s.executableSha256);
    Field(output, "shader_sha256", s.shaderSha256); Field(output, "input_sha256", r.inputSha256);
    Field(output, "expected_output_sha256", r.expectedOutputSha256); Field(output, "gpu_uuid_identity", s.condition.gpuIdentity.uuid);
    Field(output, "workload", WorkloadLabel(p)); Field(output, "variant", VariantLabel(p));
    Field(output, "generator_revision", w.common.generatorRevision); Field(output, "seed", w.common.seed); Parameters(output, p);
    Field(output, "execution_mode", computelab::ex2::ToString(w.common.executionMode));
    Field(output, "operation_boundary", computelab::ex2::ToString(w.common.operationBoundary));
    output += ",\"backend_native\":" + DiagnosticsJson(r.backendDiagnostics) + "}\n";
    return output;
}

std::string SerializeInitializationCsv(const Foundation& f, std::span<const InitializationRecord> records)
{
    ValidateInitializationRecords(f, records);
    std::string output = InitializationCsvHeader() + "\r\n";
    for (const auto& r : records)
        output += Csv({"2", r.runId, "EX-2", std::string(computelab::ex2::ToString(r.backend)), Number(r.processIndex),
            Number(r.sequenceIndex), r.category, CsvOptional(r.workload), CsvOptional(r.variant), CsvOptional(r.elementCount),
            r.metric, CsvOptional(r.durationNanoseconds), CsvOptional(r.observation)});
    return output;
}
std::string SerializeHostClockRow(const Foundation& f, const HostClockRecord& r)
{
    MatchClock(f, r);
    return Csv({"2", r.runId, r.seriesId, Number(r.processIndex), Number(r.sequenceIndex), CsvOptional(r.deltaNanoseconds)});
}
std::string SerializeHostClockCsv(const Foundation& f, std::span<const HostClockRecord> records)
{
    ValidateHostClockRecords(f, records); std::string output = HostClockHeader() + "\r\n";
    for (const auto& r : records) output += SerializeHostClockRow(f, r);
    return output;
}
std::string SerializeWarmupRow(const Foundation& f, const WarmupRecord& r)
{
    Require(!SamplePhase(f.Identity()), "warmup characterization forbidden in D1-S"); ValidateOperationRecord(f, r);
    return Csv({"2", r.plan.runId, r.plan.seriesId, Number(r.plan.process.processIndex), Number(r.sequenceIndex),
        CsvOptional(r.hostSubmissionNanoseconds), CsvOptional(r.hostWaitNanoseconds), CsvOptional(r.hostCompletionNanoseconds),
        std::string(old::ToString(r.status))});
}
std::string SerializeWarmupCsv(const Foundation& f, std::span<const WarmupRecord> records)
{
    ValidateWarmupRecords(f, records); std::string output = WarmupCsvHeader() + "\r\n";
    for (const auto& r : records) output += SerializeWarmupRow(f, r);
    return output;
}
std::string SerializeSampleRow(const Foundation& f, const SampleRecord& r)
{
    Require(SamplePhase(f.Identity()), "samples forbidden in diagnostic phase"); ValidateOperationRecord(f, r);
    const auto& p = r.plan; const auto& s = p.seriesIdentity;
    return Csv({"2", p.runId, "EX-2", p.comparisonConditionId, p.seriesId, std::string(computelab::ex2::ToString(s.backend)),
        std::string(WorkloadLabel(p)), std::string(VariantLabel(p)), Number(s.condition.workload.common.seed), Number(p.condition.elementCount),
        "", "", "", CsvOptional(p.condition.iterationCount), "", "ordinary", "H", Number(s.warmupCount), Number(s.plannedSampleCount),
        Number(s.blockIndex), Number(s.orderSlot), Number(s.processIndex), Number(r.sequenceIndex), CsvOptional(r.correctness.validationPassed),
        std::string(old::ToString(r.status)), r.failurePhase ? std::string(old::ToString(*r.failurePhase)) : "", CsvOptional(r.errorCode),
        CsvOptional(r.hostSubmissionNanoseconds), CsvOptional(r.hostWaitNanoseconds), CsvOptional(r.hostCompletionNanoseconds), ""});
}
std::string SerializeSamplesCsv(const Foundation& f, std::span<const SampleRecord> records)
{
    ValidateSampleRecords(f, records); std::string output = SamplesCsvHeader() + "\r\n";
    for (const auto& r : records) output += SerializeSampleRow(f, r);
    return output;
}
std::string SerializeSummaryJson(const Foundation& f, const SummaryRecord& r, std::span<const SampleRecord> samples)
{
    ValidateSummary(f, r, samples);
    const auto& p = r.plan; const auto& s = p.seriesIdentity;
    std::string output = "{\"schema_version\":2,\"run_id\":" + Json(p.runId);
    Field(output, "experiment_id", "EX-2"); Field(output, "protocol_version", ProtocolVersion); Field(output, "evidence_kind", EvidenceKind);
    Field(output, "process_status", old::ToString(r.processStatus));
    Field(output, "failure_phase", r.failurePhase ? std::optional<std::string>{old::ToString(*r.failurePhase)} : std::nullopt);
    Field(output, "error_code", r.errorCode);
    output += ",\"sample_groups\":[{\"group\":{\"comparison_condition_id\":" + Json(p.comparisonConditionId);
    Field(output, "series_id", p.seriesId); Field(output, "run_id", p.runId); Field(output, "backend", computelab::ex2::ToString(s.backend));
    Field(output, "workload", WorkloadLabel(p)); Field(output, "variant", VariantLabel(p)); Field(output, "seed", s.condition.workload.common.seed);
    Parameters(output, p); Field(output, "execution_mode", "ordinary"); Field(output, "instrument_mode", "H");
    Field(output, "warmup_count", s.warmupCount); Field(output, "planned_sample_count", s.plannedSampleCount);
    Field(output, "block_index", s.blockIndex); Field(output, "order_slot", s.orderSlot); Field(output, "process_index", s.processIndex);
    output += '}'; Field(output, "recorded_sample_count", r.recordedSampleCount); Field(output, "validation_failures", r.validationFailures);
    Field(output, "failed_sample_count", r.failedSampleCount);
    output += ",\"metric_admission\":{\"host_submission_ns\":" + Admission(p, MetricIdentity::HostSubmission)
        + ",\"host_wait_ns\":" + Admission(p, MetricIdentity::HostWait)
        + ",\"host_completion_ns\":" + Admission(p, MetricIdentity::HostCompletion)
        + ",\"native_device_interval_ns\":" + Admission(p, MetricIdentity::NativeDeviceInterval) + "},\"metrics\":{\"host_submission_ns\":"
        + MetricJson(r.hostSubmissionNanoseconds) + ",\"host_wait_ns\":" + MetricJson(r.hostWaitNanoseconds)
        + ",\"host_completion_ns\":" + MetricJson(r.hostCompletionNanoseconds)
        + ",\"native_device_interval_ns\":null}}]}\n";
    return output;
}

} // namespace computelab::ex2::stage5::evidence

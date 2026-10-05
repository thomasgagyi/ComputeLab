#include "ex2/Ex2Stage6Evidence.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>
namespace computelab::ex2::stage6::evidence
{
namespace old = computelab::ex2::evidence;
namespace detail
{
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

bool IsSha256(std::string_view text)
{
    return text.size() == 64 && std::all_of(text.begin(), text.end(), [](char c)
        { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
void Parameters(std::string& out, const WorkloadConfiguration& w)
{
    const auto f = Fields(w);
    Field(out, "element_count", f.elementCount); Field(out, "byte_count", f.byteCount);
    Field(out, "index_pattern", f.indexPattern); Field(out, "counter_count", f.counterCount);
    Field(out, "iteration_count", f.iterationCount); Field(out, "transfer_direction", f.transferDirection);
}
std::string WorkloadJson(const WorkloadConfiguration& w)
{
    const auto f = Fields(w);
    std::string out = "{\"workload\":" + Json(f.workload);
    Field(out, "variant", f.variant); Field(out, "seed", w.common.seed); Parameters(out, w);
    Field(out, "execution_mode", computelab::ex2::ToString(w.common.executionMode));
    Field(out, "instrument_mode", "H"); return out + '}';
}
} // namespace detail
using namespace detail;
namespace
{
void MatchPlan(const Plan& expected, const Plan& actual)
{
    ValidatePlan(expected); ValidatePlan(actual);
    Require(expected.slot == actual.slot && expected.runId == actual.runId
        && expected.comparisonConditionId == actual.comparisonConditionId && expected.seriesId == actual.seriesId
        && SeriesCanonicalJson(expected.seriesIdentity) == SeriesCanonicalJson(actual.seriesIdentity),
        "Stage-6 record identity drift");
}
Plan MakePlan(const FoundationRequest& r)
{
    ValidatePlannedSlot(r.slot); const auto& process = r.slot.process;
    Plan p{r.slot, r.runId,
        {{std::string(ProtocolVersion), r.machineId, r.gpuIdentity, WorkloadForCell(r.slot.cellIndex), Instrument},
        process.backend, process.processIndex, process.blockIndex, process.orderSlot, WarmupCount,
        PlannedSampleCount, r.sourceRevision, r.executableSha256, r.shaderSha256}, {}, {}};
    p.comparisonConditionId = ComparisonConditionId(p.seriesIdentity.condition);
    p.seriesId = SeriesId(p.seriesIdentity); ValidatePlan(p); return p;
}
void Outcome(Status status, const std::optional<FailurePhase>& phase, const std::optional<std::string>& code)
{
    if (status == Status::Ok) { Require(!phase && !code, "ok cannot carry failure context"); return; }
    Require(phase && code && IsValidAnonymousIdentifier(*code), "failure context missing");
    bool valid = false;
    switch (status)
    {
    case Status::ValidationFailed: valid = *phase == FailurePhase::Validation && *code == old::error_code::OutputMismatch; break;
    case Status::SubmitFailed: valid = *phase == FailurePhase::Submission && *code == old::error_code::SubmissionFailed; break;
    case Status::WaitFailed: valid = *phase == FailurePhase::CompletionWait && *code == old::error_code::CompletionFailed; break;
    case Status::Timeout: valid = (*phase == FailurePhase::Submission || *phase == FailurePhase::CompletionWait)
        && *code == old::error_code::OperationTimeout; break;
    case Status::DeviceLost: valid = (*phase == FailurePhase::BackendInitialization || *phase == FailurePhase::Submission
        || *phase == FailurePhase::CompletionWait || *phase == FailurePhase::Readback) && *code == old::error_code::DeviceLost; break;
    case Status::Incomplete:
        switch (*phase)
        {
        case FailurePhase::BackendInitialization: valid = *code == old::error_code::BackendInitializationFailed; break;
        case FailurePhase::ResourceAllocation: valid = *code == old::error_code::ResourceAllocationFailed; break;
        case FailurePhase::Submission: valid = *code == old::error_code::SubmissionFailed; break;
        case FailurePhase::CompletionWait: valid = *code == old::error_code::CompletionFailed; break;
        case FailurePhase::Readback: valid = *code == old::error_code::ReadbackFailed; break;
        case FailurePhase::EvidenceSerialization: valid = *code == old::error_code::EvidenceSerializationFailed; break;
        case FailurePhase::EvidencePublication: valid = *code == old::error_code::EvidencePublicationFailed; break;
        case FailurePhase::Interrupted: valid = *code == old::error_code::Interrupted; break;
        default: break;
        } break;
    default: break;
    }
    Require(valid, "inapplicable or contradictory H failure");
}
void Text(std::string_view value)
{
    Require(!value.empty() && value.size() <= 1024 && Utf8(value)
        && value.find_first_of("\\/\r\n") == std::string_view::npos
        && value.find('\0') == std::string_view::npos, "invalid environment provenance");
}
std::string DiagnosticsJson(const BackendDiagnostics& d)
{
    std::string out = "{\"implementation\":" + Json(d.implementation);
    Field(out, "stream_flags", d.streamFlags); Field(out, "queue_family_index", d.queueFamilyIndex);
    Field(out, "queue_flags", d.queueFlags); Field(out, "queue_count", d.queueCount);
    out += ",\"timestamp_valid_bits\":null,\"timestamp_period_ns\":null";
    Field(out, "input_memory_flags", d.inputMemoryFlags); Field(out, "output_memory_flags", d.outputMemoryFlags);
    Field(out, "upload_memory_flags", d.uploadMemoryFlags); Field(out, "readback_memory_flags", d.readbackMemoryFlags);
    return out + ",\"native_markers_enabled\":false,\"native_timing_method\":null,\"native_timing_resolution_ns\":null,"
        "\"native_timing_start_stage\":null,\"native_timing_stop_stage\":null,\"native_duration_envelope_ns\":null}";
}
} // namespace
void ValidatePlan(const Plan& p)
{
    ValidatePlannedSlot(p.slot); const auto& s = p.seriesIdentity; const auto& process = p.slot.process;
    Require(IsValidAnonymousIdentifier(p.runId) && s.condition.protocolVersion == ProtocolVersion
        && s.condition.workload == WorkloadForCell(p.slot.cellIndex) && s.condition.instrumentMode == Instrument
        && s.backend == process.backend && s.processIndex == process.processIndex && s.blockIndex == process.blockIndex
        && s.orderSlot == process.orderSlot && s.warmupCount == WarmupCount && s.plannedSampleCount == PlannedSampleCount,
        "Stage-6 plan disagrees with frozen controls");
    Require(s.condition.gpuIdentity.uuid != "00000000-0000-0000-0000-000000000000", "zero GPU UUID");
    Require(p.comparisonConditionId == ComparisonConditionId(s.condition) && p.seriesId == SeriesId(s), "stale identity");
}
Foundation::Foundation(Plan p, std::string input, std::string expected)
    : plan_(std::move(p)), inputSha256_(std::move(input)), expectedOutputSha256_(std::move(expected)) {}
Foundation MakeWordFoundation(const FoundationRequest& r, const InputIdentity& input, std::span<const std::uint32_t> expected)
{
    auto p = MakePlan(r); const auto& w = p.seriesIdentity.condition.workload;
    Require(input.Workload() == w && IsSha256(input.Sha256()), "typed input identity drift");
    const auto f = Fields(w); Require(f.elementCount && expected.size() == *f.elementCount, "wrong expected word type/count");
    return {std::move(p), input.Sha256(), WordInputSha256(expected)};
}
Foundation MakeByteFoundation(const FoundationRequest& r, const InputIdentity& input, std::span<const std::uint8_t> expected)
{
    auto p = MakePlan(r); const auto& w = p.seriesIdentity.condition.workload;
    Require(input.Workload() == w && IsSha256(input.Sha256()), "typed input identity drift");
    const auto f = Fields(w); Require(f.byteCount && expected.size() == *f.byteCount, "wrong expected byte type/count");
    return {std::move(p), input.Sha256(), ByteInputSha256(expected)};
}
EnvironmentRecord MakeEnvironmentRecord(results::EnvironmentRecord common, const Foundation& f, BackendDiagnostics diagnostics)
{
    EnvironmentRecord r{std::move(common), f.Identity(), f.InputSha256(), f.ExpectedOutputSha256(), std::move(diagnostics)};
    ValidateEnvironmentRecord(f, r); return r;
}
void ValidateEnvironmentRecord(const Foundation& f, const EnvironmentRecord& r)
{
    MatchPlan(f.Identity(), r.plan); const auto& c = r.common; const auto& s = r.plan.seriesIdentity;
    Require(c.schemaVersion == SchemaVersion && c.experimentId == ExperimentId && c.runId == r.plan.runId
        && c.machineId == s.condition.machineId && c.gitCommit == s.sourceRevision && r.evidenceKind == EvidenceKind,
        "environment identity drift");
    for (const auto* text : {&c.timestampUtc, &c.osName, &c.osVersion, &c.cpuName, &c.compilerName,
        &c.compilerVersion, &c.cmakeVersion, &c.ninjaVersion, &c.configurePreset, &c.buildType}) Text(*text);
    Require(c.timestampUtc.size() == 20 && c.timestampUtc[10] == 'T' && c.timestampUtc.back() == 'Z', "UTC timestamp required");
    Require(c.systemMemoryBytes && c.gpuMemoryBytes && *c.gpuMemoryBytes, "physical memory missing");
    for (const auto* text : {&c.gpuName, &c.gpuVendor, &c.gpuDeviceId, &c.nvidiaDriverVersion})
    { Require(text->has_value(), "GPU provenance missing"); Text(**text); }
    for (const auto* text : {&c.cudaToolkitVersion, &c.cudaRuntimeVersion, &c.cudaComputeCapability,
        &c.vulkanSdkVersion, &c.vulkanDeviceApiVersion}) if (*text) Text(**text);
    Require(s.backend == Backend::Cuda ? c.cudaToolkitVersion && c.cudaRuntimeVersion && c.cudaComputeCapability
        : c.vulkanSdkVersion && c.vulkanDeviceApiVersion, "backend provenance missing");
    Require(r.inputSha256 == f.InputSha256() && r.expectedOutputSha256 == f.ExpectedOutputSha256(), "environment digest drift");
    const auto& d = r.backendDiagnostics;
    Require(IsValidAnonymousIdentifier(d.implementation) && (!d.streamFlags || IsValidAnonymousIdentifier(*d.streamFlags)),
        "invalid backend diagnostic identifier");
    Require(!d.nativeMarkersEnabled && !d.timestampValidBits && !d.timestampPeriodNanoseconds && !d.nativeTimingMethod
        && !d.nativeTimingResolutionNanoseconds && !d.nativeTimingStartStage && !d.nativeTimingStopStage
        && !d.nativeDurationEnvelopeNanoseconds, "H cannot carry native timing metadata");
    Require(s.backend == Backend::Cuda ? d.streamFlags && !d.queueFamilyIndex && !d.queueFlags && !d.queueCount
        : !d.streamFlags && d.queueFamilyIndex && d.queueFlags && d.queueCount && *d.queueCount, "backend diagnostics drift");
}
InitializationRecord MakeSetupCompleteInitialization(const Foundation& f, std::string observation)
{
    const auto& p = f.Identity(); const auto fields = Fields(p.seriesIdentity.condition.workload);
    InitializationRecord r{p.runId, p.slot.process.backend, p.slot.process.processIndex, 0, "backend_setup",
        std::string(fields.workload), std::string(fields.variant), fields.elementCount, "setup_complete",
        std::nullopt, std::move(observation)};
    ValidateInitializationRecords(p, {&r, 1}); return r;
}
void ValidateInitializationRecords(const Plan& p, std::span<const InitializationRecord> records)
{
    ValidatePlan(p); Require(!records.empty(), "initialization cannot be empty"); const auto fields = Fields(p.seriesIdentity.condition.workload);
    bool setup = false;
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        const auto& r = records[i];
        Require(r.runId == p.runId && r.backend == p.slot.process.backend && r.processIndex == p.slot.process.processIndex
            && r.sequenceIndex == i, "initialization identity/sequence drift");
        Require(IsValidAnonymousIdentifier(r.category) && IsValidAnonymousIdentifier(r.metric), "invalid initialization identifier");
        Require((!r.workload || *r.workload == fields.workload) && (!r.variant || *r.variant == fields.variant)
            && (!r.elementCount || r.elementCount == fields.elementCount), "initialization workload drift");
        Require(r.durationNanoseconds || (r.observation && !r.observation->empty()), "initialization observation missing");
        if (r.observation) Require(Utf8(*r.observation) && r.observation->find('\0') == std::string::npos, "invalid initialization text");
        if (r.metric == "setup_complete")
        {
            Require(!setup && r.category == "backend_setup" && !r.durationNanoseconds && r.observation
                && !r.observation->empty(), "setup_complete must be a single truthful untimed observation"); setup = true;
        }
    }
}
SampleRecord MakeSampleRecord(const Foundation& f, std::uint64_t index)
{
    Require(index < PlannedSampleCount, "sample index outside declared sequence");
    SampleRecord r; r.plan = f.Identity(); r.sampleIndex = index; r.correctness.expectedOutputGenerated = true; return r;
}
void ValidateSampleRecord(const SampleRecord& r)
{
    ValidatePlan(r.plan); Require(r.sampleIndex < PlannedSampleCount, "invalid sample index"); Outcome(r.status, r.failurePhase, r.errorCode);
    const auto& c = r.correctness;
    Require(c.expectedOutputGenerated && (!c.outputObserved || c.operationCompleted)
        && (!c.comparisonPerformed || c.outputObserved) && c.validationPassed.has_value() == c.comparisonPerformed,
        "contradictory correctness progress");
    const bool compared = r.status == Status::Ok || r.status == Status::ValidationFailed;
    Require(compared ? c.operationCompleted && c.outputObserved && c.comparisonPerformed
        && c.validationPassed == (r.status == Status::Ok) : !c.comparisonPerformed && !c.validationPassed,
        "status and comparison disagree");
    const auto phase = r.failurePhase;
    const bool beforeCompletion = phase == FailurePhase::BackendInitialization || phase == FailurePhase::ResourceAllocation
        || phase == FailurePhase::Submission || phase == FailurePhase::CompletionWait;
    Require(!beforeCompletion || (!c.operationCompleted && !c.outputObserved), "pre-completion failure claims completion");
    Require(phase != FailurePhase::Readback || (c.operationCompleted && !c.outputObserved), "readback failure progress");
    Require(!r.nativeDeviceIntervalNanoseconds && r.status != Status::TimestampInvalid, "native timing in H");
    const bool timed = r.hostCompletionNanoseconds.has_value();
    Require(r.hostSubmissionNanoseconds.has_value() == timed && r.hostWaitNanoseconds.has_value() == timed, "partial H tuple");
    Require(timed == c.operationCompleted, "H tuple requires known completion");
    if (timed) Require(*r.hostSubmissionNanoseconds <= std::numeric_limits<std::uint64_t>::max() - *r.hostWaitNanoseconds
        && *r.hostCompletionNanoseconds == *r.hostSubmissionNanoseconds + *r.hostWaitNanoseconds, "invalid H arithmetic");
}
void ValidateSamples(const Plan& p, std::span<const SampleRecord> records)
{
    ValidatePlan(p); Require(records.size() <= PlannedSampleCount, "too many sample rows");
    for (std::size_t i = 0; i < records.size(); ++i)
    {
        const auto& r = records[i]; MatchPlan(p, r.plan); ValidateSampleRecord(r);
        Require(r.sampleIndex == i && (i + 1 == records.size() || r.status == Status::Ok), "sample prefix/terminal failure drift");
    }
}
results::MetricSummary SummarizeMetric(std::span<const std::uint64_t> input)
{
    if (input.empty()) return {};
    std::vector<double> values; values.reserve(input.size()); double sum = 0;
    for (const auto value : input) { values.push_back(static_cast<double>(value)); sum += values.back(); }
    const double mean = sum / static_cast<double>(values.size()); std::optional<double> sd, cv;
    if (values.size() >= 2)
    {
        double squared = 0;
        for (const auto value : values) { const auto difference = value - mean; squared += difference * difference; }
        sd = std::sqrt(squared / static_cast<double>(values.size() - 1)); if (mean != 0) cv = *sd / mean;
    }
    std::sort(values.begin(), values.end()); const auto middle = values.size() / 2;
    results::MetricSummary result{static_cast<std::uint64_t>(values.size()), values.front(),
        values.size() % 2 ? values[middle] : (values[middle - 1] + values[middle]) / 2.0,
        mean, sd, cv, values[values.size() - values.size() / 20 - 1]};
    (void)SerializeMetricJson(result); return result;
}
std::string SerializeMetricJson(const results::MetricSummary& m)
{
    std::string out = "{\"sample_count\":" + Json(m.sampleCount);
    Field(out, "minimum", m.minimum); Field(out, "median", m.median); Field(out, "mean", m.mean);
    Field(out, "standard_deviation", m.standardDeviation); Field(out, "coefficient_of_variation", m.coefficientOfVariation);
    Field(out, "p95", m.p95); return out + '}';
}
SummaryRecord SummarizeSamples(const Plan& p, std::span<const SampleRecord> samples, Status status,
    std::optional<FailurePhase> phase, std::optional<std::string> code)
{
    ValidateSamples(p, samples); Outcome(status, phase, code);
    if (status == Status::Ok)
        Require(samples.size() == PlannedSampleCount && samples.back().status == Status::Ok, "ok needs 100 successful rows");
    else if (!samples.empty())
        Require(samples.back().status == status && samples.back().failurePhase == phase && samples.back().errorCode == code,
            "terminal row disagrees with process failure");
    else Require(status != Status::ValidationFailed, "zero-row failure cannot claim output comparison");
    SummaryRecord r; r.plan = p; r.processStatus = status; r.failurePhase = phase; r.errorCode = std::move(code);
    r.recordedSampleCount = samples.size(); std::vector<std::uint64_t> submission, wait, completion;
    for (const auto& row : samples)
    {
        r.validationFailures += row.correctness.validationPassed == false ? 1U : 0U;
        if (row.status == Status::Ok && row.correctness.validationPassed == true)
        {
            ++r.successfulSampleCount; submission.push_back(*row.hostSubmissionNanoseconds);
            wait.push_back(*row.hostWaitNanoseconds); completion.push_back(*row.hostCompletionNanoseconds);
        }
        else ++r.failedSampleCount;
    }
    r.hostSubmissionNanoseconds = SummarizeMetric(submission); r.hostWaitNanoseconds = SummarizeMetric(wait);
    r.hostCompletionNanoseconds = SummarizeMetric(completion); return r;
}
void ValidateSummary(const SummaryRecord& r, std::span<const SampleRecord> samples)
{
    const auto expected = SummarizeSamples(r.plan, samples, r.processStatus, r.failurePhase, r.errorCode);
    Require(r.recordedSampleCount == expected.recordedSampleCount && r.successfulSampleCount == expected.successfulSampleCount
        && r.validationFailures == expected.validationFailures && r.failedSampleCount == expected.failedSampleCount
        && SerializeMetricJson(r.hostSubmissionNanoseconds) == SerializeMetricJson(expected.hostSubmissionNanoseconds)
        && SerializeMetricJson(r.hostWaitNanoseconds) == SerializeMetricJson(expected.hostWaitNanoseconds)
        && SerializeMetricJson(r.hostCompletionNanoseconds) == SerializeMetricJson(expected.hostCompletionNanoseconds)
        && !r.nativeDeviceIntervalNanoseconds, "summary disagrees with raw");
}
void ValidateBundle(const Foundation& f, const EnvironmentRecord& env, std::span<const InitializationRecord> init,
    std::span<const SampleRecord> samples, const SummaryRecord& summary)
{
    ValidateEnvironmentRecord(f, env); MatchPlan(f.Identity(), summary.plan);
    ValidateInitializationRecords(f.Identity(), init); ValidateSamples(f.Identity(), samples); ValidateSummary(summary, samples);
    const bool setup = std::any_of(init.begin(), init.end(), [](const auto& r) { return r.metric == "setup_complete"; });
    Require(samples.empty() || setup, "sampling requires setup_complete");
    const bool preSetup = summary.failurePhase == FailurePhase::BackendInitialization || summary.failurePhase == FailurePhase::ResourceAllocation;
    Require(!samples.empty() || !preSetup || !setup, "pre-setup failure cannot fabricate setup_complete");
}
std::string InitializationCsvHeader() { return old::InitializationCsvHeader(); }
std::string SamplesCsvHeader() { return old::SamplesCsvHeader(); }
std::string SerializeEnvironmentJson(const Foundation& f, const EnvironmentRecord& r)
{
    ValidateEnvironmentRecord(f, r); const auto& p = r.plan; const auto& s = p.seriesIdentity;
    const auto& w = s.condition.workload; const auto fields = Fields(w);
    std::string out = results::SerializeEnvironmentJson(r.common); out.resize(out.size() - 2);
    Field(out, "protocol_version", ProtocolVersion); Field(out, "evidence_kind", EvidenceKind);
    Field(out, "backend", computelab::ex2::ToString(s.backend)); Field(out, "instrument_mode", "H");
    Field(out, "warmup_count", s.warmupCount); Field(out, "planned_sample_count", s.plannedSampleCount);
    Field(out, "comparison_condition_id", p.comparisonConditionId); Field(out, "series_id", p.seriesId);
    Field(out, "cell_index", p.slot.cellIndex); Field(out, "slot_sequence_index", p.slot.sequenceIndex);
    Field(out, "block_index", s.blockIndex); Field(out, "process_index", s.processIndex); Field(out, "order_slot", s.orderSlot);
    Field(out, "source_revision", s.sourceRevision); Field(out, "executable_sha256", s.executableSha256);
    Field(out, "shader_sha256", s.shaderSha256); Field(out, "input_sha256", r.inputSha256);
    Field(out, "expected_output_sha256", r.expectedOutputSha256); Field(out, "gpu_uuid_identity", s.condition.gpuIdentity.uuid);
    Field(out, "workload", fields.workload); Field(out, "variant", fields.variant);
    Field(out, "generator_revision", w.common.generatorRevision); Field(out, "seed", w.common.seed); Parameters(out, w);
    Field(out, "execution_mode", computelab::ex2::ToString(w.common.executionMode));
    Field(out, "operation_boundary", computelab::ex2::ToString(w.common.operationBoundary));
    return out + ",\"backend_native\":" + DiagnosticsJson(r.backendDiagnostics) + "}\n";
}
std::string SerializeInitializationCsv(const Plan& p, std::span<const InitializationRecord> records)
{
    ValidateInitializationRecords(p, records); std::string out = InitializationCsvHeader() + "\r\n";
    for (const auto& r : records) out += Csv({"2", r.runId, "EX-2", std::string(computelab::ex2::ToString(r.backend)), Number(r.processIndex),
        Number(r.sequenceIndex), r.category, CsvOptional(r.workload), CsvOptional(r.variant), CsvOptional(r.elementCount),
        r.metric, CsvOptional(r.durationNanoseconds), CsvOptional(r.observation)});
    return out;
}
std::string SerializeSampleRow(const SampleRecord& r)
{
    ValidateSampleRecord(r); const auto& p = r.plan; const auto& s = p.seriesIdentity; const auto f = Fields(s.condition.workload);
    return Csv({"2", p.runId, "EX-2", p.comparisonConditionId, p.seriesId, std::string(computelab::ex2::ToString(s.backend)),
        std::string(f.workload), std::string(f.variant), Number(s.condition.workload.common.seed), CsvOptional(f.elementCount),
        CsvOptional(f.byteCount), f.indexPattern ? std::string(*f.indexPattern) : "", CsvOptional(f.counterCount),
        CsvOptional(f.iterationCount), f.transferDirection ? std::string(*f.transferDirection) : "",
        std::string(computelab::ex2::ToString(s.condition.workload.common.executionMode)), "H",
        Number(s.warmupCount), Number(s.plannedSampleCount), Number(s.blockIndex), Number(s.orderSlot), Number(s.processIndex),
        Number(r.sampleIndex), CsvOptional(r.correctness.validationPassed), std::string(old::ToString(r.status)),
        r.failurePhase ? std::string(old::ToString(*r.failurePhase)) : "", CsvOptional(r.errorCode),
        CsvOptional(r.hostSubmissionNanoseconds), CsvOptional(r.hostWaitNanoseconds), CsvOptional(r.hostCompletionNanoseconds), ""});
}
std::string SerializeSamplesCsv(const Plan& p, std::span<const SampleRecord> records)
{
    ValidateSamples(p, records); std::string out = SamplesCsvHeader() + "\r\n";
    for (const auto& r : records) out += SerializeSampleRow(r); return out;
}
std::string SerializeSummaryJson(const SummaryRecord& r, std::span<const SampleRecord> samples)
{
    ValidateSummary(r, samples); const auto& p = r.plan; const auto& s = p.seriesIdentity;
    const auto& w = s.condition.workload; const auto f = Fields(w);
    std::string out = "{\"schema_version\":2,\"run_id\":" + Json(p.runId);
    Field(out, "experiment_id", "EX-2"); Field(out, "protocol_version", ProtocolVersion); Field(out, "evidence_kind", EvidenceKind);
    Field(out, "process_status", old::ToString(r.processStatus));
    Field(out, "failure_phase", r.failurePhase ? std::optional<std::string>{old::ToString(*r.failurePhase)} : std::nullopt);
    Field(out, "error_code", r.errorCode);
    out += ",\"sample_groups\":[{\"group\":{\"comparison_condition_id\":" + Json(p.comparisonConditionId);
    Field(out, "series_id", p.seriesId); Field(out, "run_id", p.runId);
    Field(out, "cell_index", p.slot.cellIndex); Field(out, "slot_sequence_index", p.slot.sequenceIndex);
    Field(out, "backend", computelab::ex2::ToString(s.backend)); Field(out, "workload", f.workload); Field(out, "variant", f.variant);
    Field(out, "seed", w.common.seed); Parameters(out, w); Field(out, "execution_mode", computelab::ex2::ToString(w.common.executionMode));
    Field(out, "instrument_mode", "H"); Field(out, "warmup_count", s.warmupCount); Field(out, "planned_sample_count", s.plannedSampleCount);
    Field(out, "block_index", s.blockIndex); Field(out, "order_slot", s.orderSlot); Field(out, "process_index", s.processIndex);
    out += '}'; Field(out, "recorded_sample_count", r.recordedSampleCount); Field(out, "successful_sample_count", r.successfulSampleCount);
    Field(out, "validation_failures", r.validationFailures); Field(out, "failed_sample_count", r.failedSampleCount);
    return out + ",\"metrics\":{\"host_submission_ns\":" + SerializeMetricJson(r.hostSubmissionNanoseconds)
        + ",\"host_wait_ns\":" + SerializeMetricJson(r.hostWaitNanoseconds)
        + ",\"host_completion_ns\":" + SerializeMetricJson(r.hostCompletionNanoseconds) + ",\"native_device_interval_ns\":null}}]}\n";
}
} // namespace computelab::ex2::stage6::evidence